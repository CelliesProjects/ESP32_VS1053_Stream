#include "ESP32_VS1053_Stream.h"

ESP32_VS1053_Stream::ESP32_VS1053_Stream() : _vs1053(nullptr), _http(nullptr), _vs1053Buffer{0}, _localbuffer{0}, _url{0},
                                             _ringbuffer_handle(nullptr), _buffer_struct(nullptr), _buffer_storage(nullptr),
                                             _filesystem(nullptr) {}

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
    constexpr const auto BUFFER_SIZE = 12;
    char buffer[BUFFER_SIZE];
    int cnt = 0;

    while (cnt < BUFFER_SIZE - 1)
    {
        int currentChar = stream->read();
        if (currentChar == -1)
            return 0; // Handle read error or end of stream

        if (currentChar == '\r')
            continue;

        if (currentChar == '\n')
            break;

        buffer[cnt++] = (char)currentChar;
    }
    buffer[cnt] = '\0';

    return strtol(buffer, nullptr, 16);
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
    stopSong();
    if (audio_eof_stream)
        audio_eof_stream(_url);
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
    if (!_vs1053 || _http || _playingFile || !WiFi.isConnected() ||
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

    _http->setConnectTimeout(tolower(url[4]) == 's' ? VS1053_CONNECT_TIMEOUT_MS_SSL
                                                    : VS1053_CONNECT_TIMEOUT_MS);

    {
        auto length = strlen(url);
        auto cnt = 0;
        for (size_t i = 0; i < length; ++i)
            if (url[i] == ' ')
                ++cnt;

        char escapedUrl[cnt ? length + (3 * cnt) + 1 : 1]; // At least 1 to avoid zero-sized array

        if (cnt)
        {
            size_t in = 0;
            size_t out = 0;

            while (in < length)
            {
                if (url[in] == ' ')
                {
                    escapedUrl[out++] = '%';
                    escapedUrl[out++] = '2';
                    escapedUrl[out++] = '0';
                }
                else
                    escapedUrl[out++] = url[in];
                ++in;
            }
            escapedUrl[out] = '\0';
        }
        if (!_http->begin(cnt ? escapedUrl : url))
        {
            log_w("Could not connect to %s", url);
            stopSong();
            return false;
        }
    }

    if (offset)
    {
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "bytes=%zu-", offset);
        _http->addHeader("Range", buffer);
    }

    if (strlen(username) || strlen(pwd))
        _http->setAuthorization(username, pwd);

    _http->addHeader("Icy-MetaData", VS1053_ICY_METADATA ? "1" : "0");

    const char *header[] = {CONTENT_TYPE, ICY_NAME, ICY_METAINT,
                            ENCODING, BITRATE, LOCATION};
    _http->collectHeaders(header, sizeof(header) / sizeof(char *));
    _http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    const int result = _http->GET();

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
            const auto BYTES_TO_READ = min(stream->available(), VS1053_MAX_PLAYLIST_READ);
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
        _chunkedResponse = _http->header(ENCODING).equalsIgnoreCase("chunked") ? true : false;
        _offset = (_remainingBytes == -1) ? 0 : offset;
        _metaDataStart = _http->header(ICY_METAINT).toInt();
        _musicDataPosition = _metaDataStart ? 0 : -1;
        if (strcmp(_url, url))
        {
            _vs1053->stopSong();
            snprintf(_url, VS1053_MAX_URL_LENGTH, "%s", url);
        }
        _streamStalledTime = 0;
        log_d("redirected %i times", _redirectCount);
        _redirectCount = 0;
        _allocateRingbuffer();
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
    if (!_ringbuffer_filled)
    {
        const size_t SET_LIMIT = min(size_t(1024 * 15), VS1053_PSRAM_BUFFER_SIZE - 1024);
        const auto MINIMUM_TO_PLAY = min(size() ? size() : SET_LIMIT, SET_LIMIT);

        if (VS1053_PSRAM_BUFFER_SIZE - xRingbufferGetCurFreeSize(_ringbuffer_handle) < MINIMUM_TO_PLAY)
            return;

        _ringbuffer_filled = true;
    }

    const auto START_TIME_MS = millis();
    const auto MAX_TIME_MS = 5;
    // size_t bytesToDecoder = 0;
    while (_remainingBytes && _vs1053->data_request() && millis() - START_TIME_MS < MAX_TIME_MS)
    {
        size_t size = 0;
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(_ringbuffer_handle, &size, pdMS_TO_TICKS(0), VS1053_PLAYBUFFER_SIZE);
        static unsigned long emptyBufferStartTimeMs = 0;
        if (!data)
        {
            if (!emptyBufferStartTimeMs)
            {
                emptyBufferStartTimeMs = millis();
                emptyBufferStartTimeMs += emptyBufferStartTimeMs ? 0 : 1;
                log_e("no buffer data available");
                return;
            }
            const auto BAILOUT_MS = 2000;
            if (millis() - emptyBufferStartTimeMs > BAILOUT_MS)
            {
                log_e("buffer empty for %i ms, bailing out...", BAILOUT_MS);
                emptyBufferStartTimeMs = 0;
                _remainingBytes = 0;
                return;
            }
            return;
        }
        if (emptyBufferStartTimeMs)
        {
            log_e("buffer empty for %i ms", millis() - emptyBufferStartTimeMs);
            emptyBufferStartTimeMs = 0;
        }

        _vs1053->playChunk(data, size);
        vRingbufferReturnItem(_ringbuffer_handle, data);
        // bytesToDecoder += size;
        _remainingBytes -= _remainingBytes > 0 ? size : 0;
    }
    log_d("spend %lu ms stuffing %i bytes in decoder", millis() - START_TIME_MS, bytesToDecoder);
}

void ESP32_VS1053_Stream::_streamToRingBuffer(WiFiClient *const stream)
{
    const auto START_TIME_MS = millis();
    const auto MAX_TIME_MS = 5;
    // size_t bytesToRingBuffer = 0;
    while (stream && stream->available() && _musicDataPosition < _metaDataStart && millis() - START_TIME_MS < MAX_TIME_MS)
    {
        const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PSRAM_MAX_MOVE);
        const size_t BYTES_SAFE_TO_MOVE = min(BYTES_TO_READ, xRingbufferGetCurFreeSize(_ringbuffer_handle));
        const size_t BYTES_IN_BUFFER = stream->readBytes(_localbuffer, min((size_t)stream->available(), BYTES_SAFE_TO_MOVE));
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, pdMS_TO_TICKS(0));
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.", BYTES_IN_BUFFER);
            _remainingBytes = 0;
            return;
        }

        // bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("spend %lu ms stuffing %i bytes in ringbuffer", millis() - START_TIME_MS, bytesToRingBuffer);
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
        const auto START_TIME_MS = millis();
        const auto MAX_TIME_MS = 10;
        // size_t bytesToDecoder = 0;
        while (stream && stream->available() && _vs1053->data_request() && _remainingBytes &&
               _musicDataPosition < _metaDataStart && millis() - START_TIME_MS < MAX_TIME_MS)
        {
            const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
            const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE);
            const size_t BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min((size_t)stream->available(), BYTES_TO_READ));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _remainingBytes -= _remainingBytes > 0 ? BYTES_IN_BUFFER : 0;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            // bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("spend %lu ms stuffing %i bytes in decoder", millis() - START_TIME_MS, bytesToDecoder);
    }

    if (stream && stream->available() && _metaDataStart && _musicDataPosition == _metaDataStart)
    {
        const auto DATA_NEEDED = stream->peek() * 16 + 1;
        if (stream->available() < DATA_NEEDED)
            return;

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
    const auto START_TIME_MS = millis();
    const auto MAX_TIME_MS = 5;
    // size_t bytesToRingBuffer = 0;
    while (stream && stream->available() && _bytesLeftInChunk && xRingbufferGetCurFreeSize(_ringbuffer_handle) &&
           _musicDataPosition < _metaDataStart && millis() - START_TIME_MS < MAX_TIME_MS)
    {
        const size_t BYTES_BEFORE_META_DATA = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, BYTES_BEFORE_META_DATA);
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PSRAM_MAX_MOVE);
        const size_t BYTES_SAFE_TO_MOVE = min(BYTES_TO_READ, xRingbufferGetCurFreeSize(_ringbuffer_handle));
        const size_t BYTES_IN_BUFFER = stream->readBytes(_localbuffer, min((size_t)stream->available(), BYTES_SAFE_TO_MOVE));
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, pdMS_TO_TICKS(0));
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.");
            _remainingBytes = 0;
            return;
        }

        _bytesLeftInChunk -= BYTES_IN_BUFFER;
        // bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("spend %lu ms stuffing %i bytes in ringbuffer", millis() - START_TIME_MS, bytesToRingBuffer);
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
        const auto START_TIME_MS = millis();
        const auto MAX_TIME_MS = 10;
        // size_t bytesToDecoder = 0;
        while (stream && stream->available() && _bytesLeftInChunk && _vs1053->data_request() &&
               _musicDataPosition < _metaDataStart && millis() - START_TIME_MS < MAX_TIME_MS)
        {
            const size_t BYTES_BEFORE_META_DATA = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
            const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, BYTES_BEFORE_META_DATA);
            const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE);
            const size_t BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(size_t(stream->available()), BYTES_TO_READ));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _bytesLeftInChunk -= BYTES_IN_BUFFER;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            // bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("spend %lu ms stuffing %i bytes in decoder", millis() - START_TIME_MS, bytesToDecoder);
    }

    if (stream && stream->available() && _metaDataStart && _musicDataPosition == _metaDataStart && _bytesLeftInChunk)
    {
        const auto DATA_NEEDED = stream->peek() * 16 + 20; /* plus 20 because there could be a end-of-chunk in the data */
        if (stream->available() < DATA_NEEDED)
            return;

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

    if (!stream)
        return;

    if (!_bytesLeftInChunk && stream->available() < 20) /* make sure we dont run out of data in the next test*/
        return;

    if (!_bytesLeftInChunk && !_checkSync(stream))
    {
        _remainingBytes = 0;
        return;
    }
}

void ESP32_VS1053_Stream::loop()
{
    if (_playingFile && _file && _vs1053->data_request())
    {
        _handleLocalFile();
        return;
    }

    if (!_http)
        return;

    if (!_http->connected())
    {
        log_e("Stream disconnect");
        _eofStream();
        return;
    }

    WiFiClient *const stream = _http->getStreamPtr(); /* this WILL be a NULL ptr at the end of real files -in psram buffer mode- when al stream data is read but not yet in the decoder */
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
        if (millis() - _streamStalledTime > VS1053_NOBUFFER_TIMEOUT_MS)
        {
            log_e("Stream timeout %lu ms", VS1053_NOBUFFER_TIMEOUT_MS);
            _eofStream();
            return;
        }
        return;
    }

    if (_ringbuffer_handle && stream && !stream->available() && !_streamStalledTime)
    {
        _streamStalledTime = millis();
        _streamStalledTime += _streamStalledTime ? 0 : 1;
    }

    if (stream && stream->available() && _streamStalledTime)
    {
        log_d("Stream stalled for %lu ms", millis() - _streamStalledTime);
        _streamStalledTime = 0;
    }

    if (_startMute)
    {
        const auto WAIT_TIME_MS = ((!bitrate() && _remainingBytes == -1) ||
                                   _currentCodec == AAC || _currentCodec == AACP || _currentCodec == OGG)
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
    return _http != nullptr || _playingFile;
}

void ESP32_VS1053_Stream::stopSong()
{
    if (!_http && !_playingFile)
        return;

    _vs1053->setVolume(0);
    _currentCodec = STOPPED;

    if (_playingFile)
    {
        _file.close();
        _playingFile = false;
        return;
    }

    _http->end();
    delete _http;
    _http = nullptr;
    _deallocateRingbuffer();
    _ringbuffer_filled = false;
    _bytesLeftInChunk = 0;
    _dataSeen = false;
    _remainingBytes = 0;
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

const char *ESP32_VS1053_Stream::lastUrl()
{
    return (_http || _playingFile) ? _url : "";
}

size_t ESP32_VS1053_Stream::size()
{
    if (_playingFile)
        return _file.size();
    return _offset + (_http ? _http->getSize() != -1 ? _http->getSize() : 0 : 0);
}

size_t ESP32_VS1053_Stream::position()
{
    if (_playingFile)
        return _file.position();
    return size() ? (size() - _remainingBytes) : 0;
}

uint32_t ESP32_VS1053_Stream::bitrate()
{
    if (_playingFile)
        return 0;
    return _http ? _http->header(BITRATE).toInt() : 0;
}

const char *ESP32_VS1053_Stream::bufferStatus()
{
    if (!_ringbuffer_handle)
        return "0/0";
    static char ringbuffer_status[24];
    snprintf(ringbuffer_status, sizeof(ringbuffer_status), "%u/%u", VS1053_PSRAM_BUFFER_SIZE - xRingbufferGetCurFreeSize(_ringbuffer_handle), VS1053_PSRAM_BUFFER_SIZE);
    return ringbuffer_status;
}

void ESP32_VS1053_Stream::bufferStatus(size_t &used, size_t &capacity)
{
    used = _ringbuffer_handle ? VS1053_PSRAM_BUFFER_SIZE - xRingbufferGetCurFreeSize(_ringbuffer_handle) : 0;
    capacity = _ringbuffer_handle ? VS1053_PSRAM_BUFFER_SIZE : 0;
}

bool ESP32_VS1053_Stream::connecttofile(fs::FS &fs, const char *filename)
{
    return connecttofile(fs, filename, 0);
}

bool ESP32_VS1053_Stream::connecttofile(fs::FS &fs, const char *filename, const size_t offset)
{
    if (!_vs1053 || _playingFile || _http)
        return false;

    _file = fs.open(filename, FILE_READ, false);
    if (!_file)
    {
        log_e("could not open file");
        return false;
    }

    if (offset >= _file.size())
    {
        _file.close();
        return false;
    }

    _currentCodec = (_file.read() == 0xFF && _file.read() == 0xFB) ? MP3 : _currentCodec;
    _file.seek(0);
    _currentCodec = (_file.read() == 0x49 && _file.read() == 0x44 && _file.read() == 0x33) ? MP3 : _currentCodec;

    _file.seek(0);
    _currentCodec = (_file.read() == 0x4F && _file.read() == 0x67 && _file.read() == 0x67) ? OGG : _currentCodec;

    if (_currentCodec == STOPPED)
    {
        log_w("unsupported file");
        _file.close();
        return false;
    }

    _file.seek(offset);
    if (strcmp(filename, _url))
    {
        _vs1053->stopSong();
        snprintf(_url, VS1053_MAX_URL_LENGTH, "%s", filename);
        _vs1053->startSong();
    }
    _filesystem = &fs;
    _playingFile = true;
    _vs1053->setVolume(_volume);
    return true;
}

void ESP32_VS1053_Stream::_handleLocalFile()
{
    if (!_filesystem->exists(_url))
    {
        log_e("fs error - bailing out");
        _eofStream();
        return;
    }

    /* this loop is IO driven where -some- transactions take a serious amount of time */
    /* and because of that -sometimes- it takes much longer to finish a loop than MAX_MS suggests */
    /* sometimes up to 13-15 ms */
    const auto START_MS = millis();
    const auto MAX_MS = 5;

    while (millis() - START_MS < MAX_MS && _file.available() && _vs1053->data_request())
    {
        const size_t BYTES_IN_BUFFER =
            _file.readBytes((char *)_vs1053Buffer, min((size_t)_file.available(), VS1053_PLAYBUFFER_SIZE));
        _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
    }

    if (!_file.available() && _file.position() == _file.size())
        _eofStream();
}
