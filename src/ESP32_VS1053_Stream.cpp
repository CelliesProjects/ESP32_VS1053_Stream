#include "ESP32_VS1053_Stream.h"

ESP32_VS1053_Stream::ESP32_VS1053_Stream() : _vs1053(nullptr), _http(nullptr), _vs1053Buffer{0}, _localbuffer{0}, _url{0},
                                             _ringbuffer_handle(nullptr), _buffer_struct(nullptr), _buffer_storage(nullptr) {}

ESP32_VS1053_Stream::~ESP32_VS1053_Stream()
{
    stopSong();
    _deallocateRingbuffer();
    delete _vs1053;
}

void ESP32_VS1053_Stream::_allocateRingbuffer()
{
    if (!psramFound() || !VS1053_PSRAM_BUFFER_ENABLED)
        return;

    if (_buffer_struct || _buffer_storage || _ringbuffer_handle)
    {
        log_e("Ringbuffer pointers not NULL on allocate");
        return;
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

size_t ESP32_VS1053_Stream::_nextChunkSize(WiFiClient *stream)
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

bool ESP32_VS1053_Stream::_checkSync(WiFiClient *stream)
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
    if (!_ringbuffer_handle)
        _allocateRingbuffer();
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
        strncasecmp(url, "http", 4) != 0)
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
                            ENCODING, LOCATION};
    _http->collectHeaders(header, sizeof(header) / sizeof(char *));
    _http->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    const int HTTPresult = _http->GET();

    switch (HTTPresult)
    {
    case 206:
        log_d("server can resume");
        [[fallthrough]];
    case 200:
    {
        const String contentType = _http->header(CONTENT_TYPE);
        const char *ct = contentType.c_str();
        if (strcasestr(ct, "audio/x-scpls") ||
            strcasestr(ct, "audio/scpls") ||
            strcasestr(ct, "audio/x-mpegurl") ||
            strcasestr(ct, "application/x-mpegurl") ||
            strcasestr(ct, "audio/mpegurl"))
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

            char line[256];

            while (stream->connected() && stream->available())
            {
                size_t len = stream->readBytesUntil('\n', line, sizeof(line) - 1);
                line[len] = 0;

                char *newUrl = strstr(line, "http");
                if (newUrl)
                {
                    strtok(newUrl, "\r\n;?");
                    stopSong();
                    log_d("playlist %s reconnects to: %s", url, newUrl);
                    return connecttohost(newUrl, username, pwd, offset);
                }
            }
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
        _streamStallStartMS = 0;
        log_i("redirected %i times to %s", _redirectCount, url);
        return true;
    }

    case 301:
        [[fallthrough]];
    case 302:
    {
        if (!_canRedirect())
        {
            stopSong();
            return false;
        }

        if (!_http->hasHeader(LOCATION))
        {
            log_e("No location header redirecting from %s", url);
            stopSong();
            return false;
        }

        const String location = _http->header(LOCATION);

        // hacky solution: some items on radio-browser.info
        // have non-resolving names containing "./" in the hostname
        if (location.indexOf("./") != -1)
        {
            log_e("Invalid url %s redirecting from %s", location, url);
            stopSong();
            return false;
        }

        stopSong();
        log_d("%i redirection to: %s", HTTPresult, location.c_str());
        return connecttohost(location.c_str(), username, pwd, 0);
    }

    default:
        log_d("error %i %s", HTTPresult, _http->errorToString(HTTPresult).c_str());
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
        _bitrateTimer = millis() ?: 1;
    }

    _updateBitRate();

    [[maybe_unused]] const auto startTimeMS = millis();
    size_t bytesToDecoder = 0;
    while (_remainingBytes && _vs1053->data_request())
    {
        size_t size = 0;
        uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(_ringbuffer_handle, &size, pdMS_TO_TICKS(0), VS1053_PLAYBUFFER_SIZE);
        if (!data)
        {
            if (!_bufferStallStartMS)
            {
                _bufferStallStartMS = millis() ?: 1;
                log_w("no buffer data available");
                return;
            }

            if (millis() - _bufferStallStartMS > VS1053_PSRAM_BUFFER_TIMEOUT_MS)
            {
                log_e("buffer empty for %i ms, bailing out...", VS1053_PSRAM_BUFFER_TIMEOUT_MS);
                _bufferStallStartMS = 0;
                _remainingBytes = 0;
                return;
            }
            return;
        }
        if (_bufferStallStartMS)
        {
            log_e("buffer empty for %i ms", millis() - _bufferStallStartMS);
            _bufferStallStartMS = 0;
        }

        _vs1053->playChunk(data, size);
        vRingbufferReturnItem(_ringbuffer_handle, data);
        bytesToDecoder += size;
        _remainingBytes = (_remainingBytes > size) ? _remainingBytes - size : 0;
    }
    log_d("%lu ms moving %i bytes ringbuffer->decoder", millis() - startTimeMS, bytesToDecoder);
}

void ESP32_VS1053_Stream::_streamToRingBuffer(WiFiClient *stream)
{
    [[maybe_unused]] const auto startTimeMS = millis();
    size_t bytesToRingBuffer = 0;
    while (xRingbufferGetCurFreeSize(_ringbuffer_handle) && stream->available() &&
           _musicDataPosition < _metaDataStart && bytesToRingBuffer < 4096)
    {
        const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PSRAM_MAX_MOVE);
        const size_t BYTES_SAFE_TO_MOVE = min(BYTES_TO_READ, xRingbufferGetCurFreeSize(_ringbuffer_handle));
        const size_t BYTES_IN_BUFFER = stream->readBytes(_localbuffer, min((size_t)stream->available(), BYTES_SAFE_TO_MOVE));
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, 0);
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.", BYTES_IN_BUFFER);
            _remainingBytes = 0;
            return;
        }

        bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("%lu ms moving %i bytes stream->ringbuffer", millis() - startTimeMS, bytesToRingBuffer);
}

void ESP32_VS1053_Stream::_handleStream(WiFiClient *stream)
{
    if (!_dataSeen)
    {
        _dataSeen = true;
        _vs1053->startSong();
        _bitrateTimer = millis() ?: 1;
    }

    if (_ringbuffer_handle)
    {
        _streamToRingBuffer(stream);
        _playFromRingBuffer();
    }
    else
    {
        _updateBitRate();

        [[maybe_unused]] const auto startTimeMS = millis();
        size_t bytesToDecoder = 0;
        while (stream->available() && _vs1053->data_request() &&
               _musicDataPosition < _metaDataStart && bytesToDecoder < 2048)
        {
            const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
            const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE);
            const size_t BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min((size_t)stream->available(), BYTES_TO_READ));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _remainingBytes -= _remainingBytes > 0 ? BYTES_IN_BUFFER : 0;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("%lu ms moving %i bytes stream->decoder", millis() - startTimeMS, bytesToDecoder);
    }

    if (stream->available() && _metaDataStart && _musicDataPosition == _metaDataStart)
    {
        const auto DATA_NEEDED = stream->peek() * 16 + 1;
        if (stream->available() < DATA_NEEDED)
            return;

        const auto METALENGTH = stream->read() * 16;
        if (METALENGTH)
        {
            stream->readBytes(_localbuffer, METALENGTH);

            if (audio_showstreamtitle)
                _handleMetadata(reinterpret_cast<char *>(_localbuffer), METALENGTH);
        }

        _musicDataPosition = 0;
    }
}

void ESP32_VS1053_Stream::_chunkedStreamToRingBuffer(WiFiClient *stream)
{
    [[maybe_unused]] const auto startTimeMS = millis();
    size_t bytesToRingBuffer = 0;
    while (xRingbufferGetCurFreeSize(_ringbuffer_handle) && stream->available() && _bytesLeftInChunk &&
           _musicDataPosition < _metaDataStart && bytesToRingBuffer < 4096)
    {
        const size_t BYTES_BEFORE_META_DATA = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, BYTES_BEFORE_META_DATA);
        const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PSRAM_MAX_MOVE);
        const size_t BYTES_SAFE_TO_MOVE = min(BYTES_TO_READ, xRingbufferGetCurFreeSize(_ringbuffer_handle));
        const size_t BYTES_IN_BUFFER = stream->readBytes(_localbuffer, min((size_t)stream->available(), BYTES_SAFE_TO_MOVE));
        const BaseType_t result = xRingbufferSend(_ringbuffer_handle, _localbuffer, BYTES_IN_BUFFER, 0);
        if (result == pdFALSE)
        {
            log_e("ringbuffer failed to receive %i bytes. Closing stream.", BYTES_IN_BUFFER);
            _remainingBytes = 0;
            return;
        }

        _bytesLeftInChunk -= BYTES_IN_BUFFER;
        bytesToRingBuffer += BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
    }
    log_d("%lu ms moving %i bytes chunked->ringbuffer", millis() - startTimeMS, bytesToRingBuffer);
}

void ESP32_VS1053_Stream::_handleChunkedStream(WiFiClient *stream)
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
            _vs1053->startSong();
            _bitrateTimer = millis() ?: 1;
        }
    }

    if (_ringbuffer_handle)
    {
        _chunkedStreamToRingBuffer(stream);
        _playFromRingBuffer();
    }
    else
    {
        _updateBitRate();

        [[maybe_unused]] const auto startTimeMS = millis();
        size_t bytesToDecoder = 0;
        while (stream->available() && _vs1053->data_request() &&
               _bytesLeftInChunk && _musicDataPosition < _metaDataStart && bytesToDecoder < 2048)
        {
            const size_t BYTES_BEFORE_META_DATA = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
            const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, BYTES_BEFORE_META_DATA);
            const size_t BYTES_TO_READ = min(BYTES_AVAILABLE, VS1053_PLAYBUFFER_SIZE);
            const size_t BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(size_t(stream->available()), BYTES_TO_READ));
            _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
            _bytesLeftInChunk -= BYTES_IN_BUFFER;
            _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
            bytesToDecoder += BYTES_IN_BUFFER;
        }
        log_d("%lu ms moving %i bytes chunked->decoder", millis() - startTimeMS, bytesToDecoder);
    }

    if (stream->available() && _metaDataStart && _musicDataPosition == _metaDataStart && _bytesLeftInChunk)
    {
        const auto DATA_NEEDED = stream->peek() * 16 + 20; /* extra margin for chunk end */
        if (stream->available() < DATA_NEEDED)
            return;

        const auto METALENGTH = stream->read() * 16;
        _bytesLeftInChunk--;

        if (METALENGTH)
        {
            size_t cnt = 0;

            while (cnt < METALENGTH)
            {
                if (!_bytesLeftInChunk)
                {
                    if (!_checkSync(stream))
                    {
                        _remainingBytes = 0;
                        return;
                    }

                    _bytesLeftInChunk = _nextChunkSize(stream);
                    if (!_bytesLeftInChunk)
                    {
                        _remainingBytes = 0;
                        return;
                    }
                }

                _localbuffer[cnt++] = stream->read();
                _bytesLeftInChunk--;
            }

            if (audio_showstreamtitle)
                _handleMetadata(reinterpret_cast<char *>(_localbuffer), METALENGTH);
        }

        _musicDataPosition = 0;
    }

    if (!_bytesLeftInChunk && stream->available() < 20) /* make sure we dont run out of data in the next test*/
        return;

    if (!_bytesLeftInChunk && !_checkSync(stream))
    {
        _remainingBytes = 0;
        return;
    }
}

void ESP32_VS1053_Stream::_feedDecoder(WiFiClient *stream)
{
    if (_chunkedResponse)
        _handleChunkedStream(stream);
    else
        _handleStream(stream);

    if (!_remainingBytes)
        _eofStream();
}

void ESP32_VS1053_Stream::loop()
{
    if (_playingFile)
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

    WiFiClient *stream = _http->getStreamPtr();
    if (!stream)
    {
        log_e("Stream connection lost");
        _eofStream();
        return;
    }

    const bool data = stream->available();
    const auto now = millis();
    const auto currentStallTimeMS = now - _streamStallStartMS;

    if (!data && _streamStallStartMS && !_ringbuffer_handle &&
        currentStallTimeMS > VS1053_STREAM_TIMEOUT_MS)
    {
        log_e("Stream timeout %lu ms", VS1053_STREAM_TIMEOUT_MS);
        _eofStream();
        return;
    }

    if (!data && !_streamStallStartMS)
    {
        _streamStallStartMS = now ?: 1;
        if (!_ringbuffer_handle)
            return;
    }

    if (data && _streamStallStartMS)
    {
        if (!_ringbuffer_handle)
            log_w("Stream stalled for %lu ms", currentStallTimeMS);
        _streamStallStartMS = 0;
    }

    _feedDecoder(stream);
}

bool ESP32_VS1053_Stream::isRunning()
{
    return _http != nullptr || _playingFile;
}

void ESP32_VS1053_Stream::stopSong()
{
    if (!_http && !_playingFile)
        return;

    _remainingBytes = 0;
    _offset = 0;
    _bitrate = 0;
    _bitrateTimer = 0;
    _codec = CODEC_UNKNOWN;
    _decoderSyncAttempts = 0;

    if (_ringbuffer_handle)
    {
        size_t size;
        void *item;
        while ((item = xRingbufferReceive(_ringbuffer_handle, &size, 0)) != nullptr)
            vRingbufferReturnItem(_ringbuffer_handle, item);
        _ringbuffer_filled = false;
        _bufferStallStartMS = 0;
    }

    if (_playingFile)
    {
        _file.close();
        _playingFile = false;
        return;
    }

    _http->end();
    delete _http;
    _http = nullptr;
    _bytesLeftInChunk = 0;
    _redirectCount = 0;
    _dataSeen = false;
}

uint8_t ESP32_VS1053_Stream::getVolume()
{
    return _volume;
}

void ESP32_VS1053_Stream::setVolume(const uint8_t newVolume)
{
    _volume = min(VS1053_MAXVOLUME, newVolume);
    if (_vs1053)
        _vs1053->setVolume(_volume);
}

void ESP32_VS1053_Stream::setTone(uint8_t *rtone)
{
    if (_vs1053)
        _vs1053->setTone(rtone);
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

    if (!_ringbuffer_handle)
    {
        log_e("psram buffer required for local file decoding");
        return false;
    }
    _file = fs.open(filename, FILE_READ, false);
    if (!_file)
    {
        log_e("could not open file");
        return false;
    }
    _file.setBufferSize(2048);

    if (offset >= _file.size())
    {
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
    _playingFile = true;
    _remainingBytes = _file.size() - offset;

    return true;
}

void ESP32_VS1053_Stream::_handleLocalFile()
{
    if (!_file)
    {
        log_e("file error");
        _eofStream();
        return;
    }

    log_d("file pos: %lu", _file.position());
    log_d("remaining bytes: %lu", _remainingBytes);

    [[maybe_unused]] const auto startTimeMS = millis();

    if (_remainingBytes && _file.position() < _file.size())
    {
        size_t free = xRingbufferGetCurFreeSize(_ringbuffer_handle);
        if (free > 1024)
        {
            size_t toRead = min(sizeof(_localbuffer), free);
            size_t bytes = _file.read(_localbuffer, toRead);

            if (bytes)
            {
                if (xRingbufferSend(_ringbuffer_handle, _localbuffer, bytes, 0) == pdFALSE)
                {
                    log_e("ringbuffer failed to receive %i bytes. Closing stream.", bytes);
                    _remainingBytes = 0;
                }

                log_d("%lu ms moving %i bytes localfile->ringbuffer", millis() - startTimeMS, bytes);
            }
        }
    }

    if (_remainingBytes)
        _playFromRingBuffer();
    else
        _eofStream();
}

void ESP32_VS1053_Stream::_updateBitRate()
{
    if (millis() - _bitrateTimer > 250)
    {
        _readBitRate();
        _bitrateTimer = millis() ?: 1;
    }
}

void ESP32_VS1053_Stream::_readBitRate()
{
    if (_codec != CODEC_UNKNOWN && !_bitrateCallback)
        return;

    const uint8_t SCI_HDAT0 = 0x08;
    const uint8_t SCI_HDAT1 = 0x09;

    uint16_t hdat1 = _vs1053->readRegister(SCI_HDAT1);
    uint16_t hdat0 = _vs1053->readRegister(SCI_HDAT0);

    if (hdat1 == 0 && hdat0 == 0) // decoder not locked yet
    {
        if (++_decoderSyncAttempts > 2)
        {
            log_w("decoder failed to sync");
            _eofStream();
        }
        return;
    }

    if (_codec == CODEC_UNKNOWN)
    {
        switch (hdat1)
        {
        case 0x4154:
            _codec = CODEC_AAC_ADTS;
            break;

        case 0x4144:
            _codec = CODEC_AAC_ADIF;
            break;

        case 0x4D34:
            _codec = CODEC_AAC_MP4;
            break;

        case 0x7665:
            _codec = CODEC_WAV;
            break;

        case 0x574D:
            _codec = CODEC_WMA;
            break;

        case 0x4D54:
            _codec = CODEC_MIDI;
            break;

        case 0x4F67:
            _codec = CODEC_OGG;
            break;

        default:
            if ((hdat1 & 0xFFE0) == 0xFFE0)
                _codec = CODEC_MP3;
        }

        if (_codec != CODEC_UNKNOWN && _codecCallback)
        {
            _codecCallback(_codecName(_codec));
            return;
        }
    }

    if (!_bitrateCallback)
        return;

    uint32_t bitrate = 0;

    if (_codec == CODEC_MP3)
    {
        uint8_t version = (hdat1 >> 3) & 0x03;
        uint8_t layer = (hdat1 >> 1) & 0x03;
        uint8_t brIndex = (hdat0 >> 12) & 0x0F;

        static const uint16_t bitrateTable[2][16] =
            {
                {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
                {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}};

        constexpr uint8_t MPEG_LAYER_III = 1;

        if (layer == MPEG_LAYER_III)
            bitrate = bitrateTable[version == 3 ? 0 : 1][brIndex];
    }
    else
        bitrate = (hdat0 * 8) / 1000;

    if (bitrate != _bitrate)
    {
        _bitrate = bitrate;
        _bitrateCallback(bitrate);
    }
}

const char *ESP32_VS1053_Stream::_codecName(uint8_t codec)
{
    const char *_names[9] = {"UNKNOWN", "AAC ADTS", "AAC ADIF", "AAC MP4", "WAV", "WMA", "MIDI", "MP3", "OGG"};

    if (codec >= sizeof(_names) / sizeof(_names[0]))
        return _names[CODEC_UNKNOWN];
    return _names[codec];
}

void ESP32_VS1053_Stream::setCodecCallback(codec_callback_t cb)
{
    _codecCallback = cb;
}

void ESP32_VS1053_Stream::clearCodecCallback()
{
    _codecCallback = nullptr;
}

void ESP32_VS1053_Stream::setBitrateCallback(bitrate_callback_t cb)
{
    _bitrateCallback = cb;
}

void ESP32_VS1053_Stream::clearBitrateCallback()
{
    _bitrateCallback = nullptr;
}
