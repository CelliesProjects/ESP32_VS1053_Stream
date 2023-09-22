#include "ESP32_VS1053_Stream.h"

ESP32_VS1053_Stream::ESP32_VS1053_Stream() : _vs1053(nullptr), _http(nullptr), _vs1053Buffer{0}, _localbuffer{0}, _url{0},
                                             _ringbuffer_handle(nullptr), _buffer_struct(nullptr), _buffer_storage(nullptr) {}

ESP32_VS1053_Stream::~ESP32_VS1053_Stream()
{
    stopSong();
    delete _vs1053;
}

void ESP32_VS1053_Stream::_allocateRingbuffer()
{
    if (!psramFound() || !VS1053_PSRAM_BUFFER_ENABLED)
        return;

    if (_buffer_struct || _buffer_storage || _ringbuffer_handle)
    {
        log_e("fatal error! Ringbuffer pointers not NULL on allocate!");
        while (true)
            delay(1000);
    }

    _buffer_struct = (StaticRingbuffer_t *)heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_SPIRAM);
    if (!_buffer_struct)
    {
        log_e("Could not allocate ringbuffer struct");
        return;
    }

    _buffer_storage = (uint8_t *)heap_caps_malloc(sizeof(uint8_t) * VS1053_PSRAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!_buffer_storage)
    {
        log_e("Could not allocate ringbuffer storage");
        free(_buffer_struct);
        _buffer_struct = nullptr;
        return;
    }

    _ringbuffer_handle = xRingbufferCreateStatic(VS1053_PSRAM_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF, _buffer_storage, _buffer_struct);
    if (!_ringbuffer_handle)
    {
        log_e("Could not create ringbuffer handle");
        free(_buffer_storage);
        _buffer_storage = nullptr;

        free(_buffer_struct);
        _buffer_struct = nullptr;
        return;
    }
    else
        log_d("Allocated %i bytes ringbuffer in PSRAM", VS1053_PSRAM_BUFFER_SIZE);
}

void ESP32_VS1053_Stream::_deallocateRingbuffer()
{
    if (_ringbuffer_handle)
    {
        vRingbufferDelete(_ringbuffer_handle);
        _ringbuffer_handle = nullptr;

        free(_buffer_storage);
        _buffer_storage = nullptr;

        free(_buffer_struct);
        _buffer_struct = nullptr;
    }
}

size_t ESP32_VS1053_Stream::_nextChunkSize(WiFiClient *const stream)
{
    constexpr const auto BUFFER_SIZE = 8;
    char buffer[BUFFER_SIZE];
    auto cnt = 0;
    char currentChar = (char)stream->read();
    while (currentChar != '\n' && cnt < BUFFER_SIZE)
    {
        buffer[cnt++] = currentChar;
        currentChar = (char)stream->read();
    }
    return strtol(buffer, NULL, 16);
}

bool ESP32_VS1053_Stream::_checkSync(WiFiClient *const stream)
{
    if ((char)stream->read() != '\r' || (char)stream->read() != '\n')
    {
        log_e("Lost sync!");
        return false;
    }
    return true;
}

void ESP32_VS1053_Stream::_handleMetadata(char *data, const size_t len)
{
    char *pch = strstr(data, "StreamTitle");
    if (!pch)
        return;
    pch = strstr(pch, "'");
    if (!pch)
        return;
    char *index = ++pch;
    while (index[0] != '\'' || index[1] != ';')
        if (index++ == data + len)
            return;
    index[0] = 0;
    audio_showstreamtitle(pch);
}

void ESP32_VS1053_Stream::_eofStream()
{
    if (audio_eof_stream)
    {
        char tmp[strlen(_url) + 1];
        snprintf(tmp, sizeof(tmp), "%s", _url);
        stopSong();
        audio_eof_stream(tmp);
    }
    else
        stopSong();
}

inline __attribute__((always_inline)) bool
ESP32_VS1053_Stream::_networkIsActive()
{
    for (int i = TCPIP_ADAPTER_IF_STA; i < TCPIP_ADAPTER_IF_MAX; i++)
        if (tcpip_adapter_is_netif_up((tcpip_adapter_if_t)i))
            return true;
    return false;
}

bool ESP32_VS1053_Stream::_canRedirect()
{
    if (_redirectCount < VS1053_MAX_REDIRECT_COUNT)
    {
        _redirectCount++;
        log_d("redirection %i", _redirectCount);
        return true;
    }
    log_w("Max redirect count (%i) reached", _redirectCount);
    return false;
}

bool ESP32_VS1053_Stream::startDecoder(const uint8_t CS, const uint8_t DCS, const uint8_t DREQ)
{
    if (_vs1053)
        return false;
    _vs1053 = new VS1053(CS, DCS, DREQ);
    if (!_vs1053)
        return false;
    _vs1053->begin();
    _vs1053->switchToMp3Mode();
    if (_vs1053->getChipVersion() == 4)
        _vs1053->loadDefaultVs1053Patches();
    setVolume(_volume);
    return true;
}

bool ESP32_VS1053_Stream::isChipConnected()
{
    return _vs1053 ? _vs1053->isChipConnected() : false;
}

bool ESP32_VS1053_Stream::connecttohost(const char *url)
{
    return connecttohost(url, "", "", 0);
}

bool ESP32_VS1053_Stream::connecttohost(const char *url, const size_t offset)
{
    return connecttohost(url, "", "", offset);
}

bool ESP32_VS1053_Stream::connecttohost(const char *url, const char *username,
                                        const char *pwd)
{
    return connecttohost(url, username, pwd, 0);
}

bool ESP32_VS1053_Stream::connecttohost(const char *url, const char *username,
                                        const char *pwd, size_t offset)
{
    if (!_vs1053 || _http || !_networkIsActive() ||
        tolower(url[0]) != 'h' ||
        tolower(url[1]) != 't' ||
        tolower(url[2]) != 't' ||
        tolower(url[3]) != 'p')
        return false;

    if (strstr(url, "./"))
    { // hacky solution: some items on radio-browser.info has
      // non resolving names that contain './' in their hostname
        log_e("Invalid url not started");
        return false;
    }

    _http = new HTTPClient;
    if (!_http)
        return false;

    [[maybe_unused]] const auto startTime = millis();

    {
        auto cnt = 0;
        auto index = 0;
        while (index < strlen(url))
            cnt += (url[index++] == ' ') ? 1 : 0;
        char escapedUrl[cnt ? strlen(url) + (3 * cnt) + 1 : 0];
        if (cnt)
        {
            auto in = 0;
            auto out = 0;
            while (in < strlen(url))
            {
                if (url[in] == ' ')
                {
                    escapedUrl[out++] = '%';
                    escapedUrl[out++] = '2';
                    escapedUrl[out++] = '0';
                }
                else
                    escapedUrl[out++] = url[in];
                in++;
            }
            escapedUrl[out] = 0;
        }

        if (!_http->begin(cnt ? escapedUrl : url))
        {
            log_w("Could not connect to %s", url);
            stopSong();
            return false;
        }
    }

    char buffer[30];
    snprintf(buffer, sizeof(buffer), "bytes=%zu-", offset);
    _http->addHeader("Range", buffer);
    _http->addHeader("Icy-MetaData", VS1053_ICY_METADATA ? "1" : "0");
    _http->setAuthorization(username, pwd);

    const char *CONTENT_TYPE = "Content-Type";
    const char *ICY_NAME = "icy-name";
    const char *ICY_METAINT = "icy-metaint";
    const char *ENCODING = "Transfer-Encoding";
    const char *BITRATE = "icy-br";
    const char *LOCATION = "Location";

    const char *header[] = {CONTENT_TYPE, ICY_NAME, ICY_METAINT,
                            ENCODING, BITRATE, LOCATION};
    _http->collectHeaders(header, sizeof(header) / sizeof(char *));
    _http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    _http->setConnectTimeout(url[4] == 's' ? VS1053_CONNECT_TIMEOUT_MS_SSL
                                           : VS1053_CONNECT_TIMEOUT_MS);

    const int result = _http->GET();
    log_d("Time elapsed during connect: %i", millis() - startTime);

    switch (result)
    {
    case 206:
        log_d("server can resume");
        [[fallthrough]];
    case 200:
    {
        String CONTENT = _http->header(CONTENT_TYPE);
        CONTENT.toLowerCase();
        if (CONTENT.indexOf("audio/scpls") != -1 ||
            CONTENT.indexOf("audio/x-scpls") != -1 ||
            CONTENT.indexOf("audio/mpegurl") != -1 ||
            CONTENT.indexOf("audio/x-mpegurl") != -1 ||
            CONTENT.indexOf("application/x-mpegurl") != -1 ||
            CONTENT.indexOf("application/pls+xml") != -1 ||
            CONTENT.indexOf("application/vnd.apple.mpegurl") != -1)
        {
            log_d("url %s is a playlist", url);
            if (!_canRedirect())
            {
                stopSong();
                return false;
            }
            WiFiClient *stream = _http->getStreamPtr();
            if (!stream)
            {
                log_e("No stream handle");
                stopSong();
                return false;
            }
            const auto BYTES_TO_READ =
                min(stream->available(), VS1053_MAX_PLAYLIST_READ);
            if (!BYTES_TO_READ)
            {
                log_e("playlist contains no data");
                stopSong();
                return false;
            }
            char file[BYTES_TO_READ + 1];
            stream->readBytes(file, BYTES_TO_READ);
            file[BYTES_TO_READ] = 0;
            char *newurl = strstr(file, "http");
            if (!newurl)
            {
                log_e("playlist contains no url");
                stopSong();
                return false;
            }
            strtok(newurl, "\r\n;?");
            stopSong();
            log_d("playlist reconnects to: %s", newurl);
            return connecttohost(newurl, username, pwd, offset);
        }

        else if (CONTENT.equals("audio/mpeg"))
            _currentCodec = MP3;

        else if (CONTENT.equals("audio/ogg") ||
                 CONTENT.equals("application/ogg"))
            _currentCodec = OGG;

        else if (CONTENT.equals("audio/aac"))
            _currentCodec = AAC;

        else if (CONTENT.equals("audio/aacp"))
            _currentCodec = AACP;

        else
        {
            log_e("closing - unsupported mimetype: '%s'", CONTENT.c_str());
            stopSong();
            return false;
        }

        if (audio_showstation && !_http->header(ICY_NAME).equals(""))
            audio_showstation(_http->header(ICY_NAME).c_str());

        _remainingBytes = _http->getSize(); // -1 when Server sends no Content-Length header (chunked streams)
        _chunkedResponse = _http->header(ENCODING).equals("chunked") ? true : false;
        _offset = (_remainingBytes == -1) ? 0 : offset;
        _metaDataStart = _http->header(ICY_METAINT).toInt();
        _musicDataPosition = _metaDataStart ? 0 : -100;
        _bitrate = _http->header(BITRATE).toInt();
        snprintf(_url, sizeof(_url), "%s", url);
        _streamStalledTime = 0;
        log_d("redirected %i times", _redirectCount);
        _redirectCount = 0;
#ifdef CONFIG_IDF_TARGET_ESP32S3
        _allocateRingbuffer();
#endif
        return true;
    }

    case 301:
        [[fallthrough]];
    case 302:
        if (!_canRedirect())
        {
            stopSong();
            return false;
        }
        if (_http->hasHeader(LOCATION) && _http->header(LOCATION).indexOf("./") == -1)
        { // hacky solution: some items on radio-browser.info
          // has non resolving names that contain './' in their
          // hostname
            char newurl[_http->header(LOCATION).length() + 1];
            snprintf(newurl, sizeof(newurl), "%s", _http->header(LOCATION).c_str());
            stopSong();
            log_d("%i redirection to: %s", result, newurl);
            return connecttohost(newurl, username, pwd, 0);
        }
        log_e("Something went wrong redirecting from %s", url);
        _redirectCount = 0;
        stopSong();
        return false;

    default:
        log_e("error %i %s", result, _http->errorToString(result).c_str());
        stopSong();
        return false;
    }
}

void ESP32_VS1053_Stream::_playFromRingBuffer()
{
    if (!_ringbuffer_filled && !size())
    {
        if (!_ringbuffer_filled && xRingbufferGetCurFreeSize(_ringbuffer_handle) > 1024)
            return;
        else
            _ringbuffer_filled = true;
    }

    size_t bytesToDecoder = 0;
    while (_remainingBytes && _vs1053->data_request() && bytesToDecoder < VS1053_MAX_BYTES_PER_LOOP)
    {
        size_t size = 0;
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(_ringbuffer_handle, &size, pdMS_TO_TICKS(0), VS1053_PLAYBUFFER_SIZE);
        if (!data)
        {
            log_d("No ringbuffer data available");
            break;
        }
        _vs1053->playChunk(data, size);
        vRingbufferReturnItem(_ringbuffer_handle, data);
        bytesToDecoder += size;
        _remainingBytes -= _remainingBytes > 0 ? size : 0;
    }
    log_d("%i bytes from ringbuffer to decoder", bytesToDecoder);
}

void ESP32_VS1053_Stream::_streamToRingBuffer(WiFiClient *const stream)
{
    size_t bytesToRingBuffer = 0;
    while (stream->available() && _musicDataPosition < _metaDataStart &&
           bytesToRingBuffer < VS1053_MAX_BYTES_PER_LOOP)
    {
        const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, sizeof(_localbuffer));

        if (xRingbufferGetCurFreeSize(_ringbuffer_handle) < BYTES_TO_READ)
            break;

        const int BYTES_IN_BUFFER = stream->readBytes(_localbuffer, BYTES_TO_READ);
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, pdMS_TO_TICKS(0));
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.");
            _remainingBytes = 0;
            return;
        }

        bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("%i bytes to ringbuffer", bytesToRingBuffer);
}

void ESP32_VS1053_Stream::_handleStream(WiFiClient *const stream)
{
    if (!_dataSeen)
    {
        _dataSeen = true;
        _startMute = millis();
        _startMute += _startMute ? 0 : 1;
        _vs1053->setVolume(0);
        _vs1053->startSong();
    }

    if (_ringbuffer_handle)
    {
        _streamToRingBuffer(stream);
        _playFromRingBuffer();
    }
    else
    {
        size_t bytesToDecoder = 0;
        while (stream->available() && _vs1053->data_request() && _remainingBytes &&
               _musicDataPosition < _metaDataStart && bytesToDecoder < VS1053_MAX_BYTES_PER_LOOP)
        {
            const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition
                                                          : stream->available();
            const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _remainingBytes -= _remainingBytes > 0 ? BYTES_IN_BUFFER : 0;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("%i bytes from stream to decoder", bytesToDecoder);
    }

    if (_metaDataStart && _musicDataPosition == _metaDataStart && stream->available())
    {
        const auto METALENGTH = stream->read() * 16;
        if (METALENGTH)
        {
            char data[METALENGTH];
            stream->readBytes(data, METALENGTH);
            if (audio_showstreamtitle)
                _handleMetadata(data, METALENGTH);
        }
        _musicDataPosition = 0;
    }
}

void ESP32_VS1053_Stream::_chunkedStreamToRingBuffer(WiFiClient *const stream)
{
    size_t bytesToRingBuffer = 0;
    while (_bytesLeftInChunk && _musicDataPosition < _metaDataStart &&
           bytesToRingBuffer < VS1053_MAX_BYTES_PER_LOOP)
    {
        const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, (size_t)_metaDataStart - _musicDataPosition);
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, sizeof(_localbuffer));

        if (xRingbufferGetCurFreeSize(_ringbuffer_handle) < BYTES_TO_READ)
            break;

        const int BYTES_IN_BUFFER = stream->readBytes(_localbuffer, BYTES_TO_READ);
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, pdMS_TO_TICKS(0));
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.");
            _remainingBytes = 0;
            return;
        }

        _bytesLeftInChunk -= BYTES_IN_BUFFER;
        bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("%i bytes from chunked stream to decoder", bytesToRingBuffer);
}

void ESP32_VS1053_Stream::_handleChunkedStream(WiFiClient *const stream)
{
    if (!_bytesLeftInChunk)
    {
        _bytesLeftInChunk = _nextChunkSize(stream);
        if (!_bytesLeftInChunk)
        {
            _remainingBytes = 0;
            return;
        }
        if (!_dataSeen)
        {
            _dataSeen = true;
            _startMute = millis();
            _startMute += _startMute ? 0 : 1;
            _vs1053->setVolume(0);
            _vs1053->startSong();
        }
    }

    if (_ringbuffer_handle)
    {
        _chunkedStreamToRingBuffer(stream);
        _playFromRingBuffer();
    }
    else
    {
        size_t bytesToDecoder = 0;
        while (_bytesLeftInChunk && _vs1053->data_request() && _musicDataPosition < _metaDataStart && bytesToDecoder < VS1053_MAX_BYTES_PER_LOOP)
        {
            const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, (size_t)_metaDataStart - _musicDataPosition);
            const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _bytesLeftInChunk -= BYTES_IN_BUFFER;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("bytes to decoder: %i", bytesToDecoder);
    }

    if (_metaDataStart && _musicDataPosition == _metaDataStart && _bytesLeftInChunk)
    {
        const auto METALENGTH = stream->read() * 16;
        _bytesLeftInChunk--;
        if (METALENGTH)
        {
            char data[METALENGTH];
            auto cnt = 0;
            while (cnt < METALENGTH)
            {
                if (!_bytesLeftInChunk)
                {
                    if (!_checkSync(stream))
                    {
                        _remainingBytes = 0;
                        return;
                    }
                    else
                    {
                        _bytesLeftInChunk = _nextChunkSize(stream);
                        if (!_bytesLeftInChunk)
                        {
                            _remainingBytes = 0;
                            return;
                        }
                    }
                }
                data[cnt++] = stream->read();
                _bytesLeftInChunk--;
            }
            if (audio_showstreamtitle)
                _handleMetadata(data, METALENGTH);
        }
        _musicDataPosition = 0;
    }

    if (!_bytesLeftInChunk && !_checkSync(stream))
    {
        _remainingBytes = 0;
        return;
    }
}

void ESP32_VS1053_Stream::loop()
{
    if (!_http)
        return;

    if (!_http->connected())
    {
        log_e("Stream disconnect");
        _eofStream();
        return;
    }

    WiFiClient *const stream = _http->getStreamPtr();
    if (!_ringbuffer_handle && !stream)
    {
        log_e("Stream connection lost");
        _eofStream();
        return;
    }

    if (!_ringbuffer_handle && !stream->available())
    {
        if (!_streamStalledTime)
        {
            _streamStalledTime = millis();
            _streamStalledTime += _streamStalledTime ? 0 : 1;
            return;
        }
        if (millis() - _streamStalledTime > VS1053_DATA_TIMEOUT_MS)
        {
            log_e("Stream timeout %lu ms", VS1053_DATA_TIMEOUT_MS);
            _eofStream();
            return;
        }
    }

    if (_streamStalledTime)
    {
        log_i("Stream stalled for %lu ms", millis() - _streamStalledTime);
        _streamStalledTime = 0;
    }

    if (_startMute)
    {
        const auto WAIT_TIME_MS = ((!_bitrate && _remainingBytes == -1) || _currentCodec == AAC || _currentCodec == AACP)
                                      ? 380
                                      : 80;
        if (millis() - _startMute > WAIT_TIME_MS)
        {
            _vs1053->setVolume(_volume);
            log_d("startmute is %lu milliseconds", WAIT_TIME_MS);
            _startMute = 0;
        }
    }

    if (_remainingBytes && _vs1053->data_request())
    {
        if (_chunkedResponse)
            _handleChunkedStream(stream);
        else
            _handleStream(stream);
    }

    if (!_remainingBytes)
        _eofStream();
}

bool ESP32_VS1053_Stream::isRunning()
{
    return _http != nullptr;
}

void ESP32_VS1053_Stream::stopSong()
{
    if (!_http)
        return;
    _vs1053->setVolume(0);
    if (_http->connected())
    {
        WiFiClient *const stream = _http->getStreamPtr();
        if (stream)
            stream->stop();
    }
    _http->end();
    delete _http;
    _http = nullptr;
    _vs1053->stopSong();
#ifdef CONFIG_IDF_TARGET_ESP32S3
    _deallocateRingbuffer();
#endif
    _ringbuffer_filled = false;
    _dataSeen = false;
    _remainingBytes = 0;
    _bytesLeftInChunk = 0;
    _currentCodec = STOPPED;
    _url[0] = 0;
    _bitrate = 0;
    _offset = 0;
}

uint8_t ESP32_VS1053_Stream::getVolume()
{
    return _volume;
}

void ESP32_VS1053_Stream::setVolume(const uint8_t newVolume)
{
    _volume = min(VS1053_MAXVOLUME, newVolume);
    if (_vs1053 && !_startMute)
        _vs1053->setVolume(_volume);
}

void ESP32_VS1053_Stream::setTone(uint8_t *rtone)
{
    if (_vs1053)
        _vs1053->setTone(rtone);
}

const char *ESP32_VS1053_Stream::currentCodec()
{
    const char *name[] = {"STOPPED", "MP3", "OGG", "AAC", "AAC+"};
    return name[_currentCodec];
}

const char *ESP32_VS1053_Stream::lastUrl() { return _url; }

size_t ESP32_VS1053_Stream::size()
{
    return _offset + (_http ? _http->getSize() != -1 ? _http->getSize() : 0 : 0);
}

size_t ESP32_VS1053_Stream::position()
{
    return size() ? (size() - _remainingBytes) : 0;
}

uint32_t ESP32_VS1053_Stream::bitrate()
{
    return _bitrate;
}
