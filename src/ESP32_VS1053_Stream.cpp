#include "ESP32_VS1053_Stream.h"

static const auto MAX_BYTES_PER_LOOP = 16 * 1024; /* do not hog the system - some files have a lot of metadata at the start */

static VS1053* _vs1053 = NULL;
static HTTPClient* _http = NULL;
static String _url;
static String _user;
static String _pwd;
static unsigned long _startMute = 0; /* mutes the sound during stream startup to supress the crack that comes with starting a stream */
static size_t _offset = 0;
static size_t _remainingBytes = 0;
static size_t _bytesLeftInChunk = 0;
static int32_t _metaDataStart = 0;
static int32_t _musicDataPosition = 0; /* position within current music data block */
static uint8_t _volume = VS1053_INITIALVOLUME;
static int _bitrate = 0;
static bool _chunkedResponse = false;
static bool _bufferFilled = false;
static bool _dataSeen = false;

static uint8_t _vs1053Buffer[VS1053_PACKETSIZE];

static enum mimetype_t {
    MP3,
    OGG,
    AAC,
    AACP,
    STOPPED
} _currentMimetype = STOPPED;

static const char* _mimestr[] = {"MP3", "OGG", "AAC", "AAC+", "STOPPED"};

inline __attribute__((always_inline))
static bool networkIsActive() {
    for (int i = TCPIP_ADAPTER_IF_STA; i < TCPIP_ADAPTER_IF_MAX; i++)
        if (tcpip_adapter_is_netif_up((tcpip_adapter_if_t)i)) return true;
    return false;
}

ESP32_VS1053_Stream::ESP32_VS1053_Stream() {}

ESP32_VS1053_Stream::~ESP32_VS1053_Stream() {
    stopSong();
    if (_vs1053) {
        delete _vs1053;
        _vs1053 = NULL;
    }
}

bool ESP32_VS1053_Stream::startDecoder(const uint8_t CS, const uint8_t DCS, const uint8_t DREQ) {
    if (_vs1053) {
        log_e("vs1053 is already initialized");
        return false;
    }
    _vs1053 = new VS1053(CS, DCS, DREQ);
    if (!_vs1053) {
        log_e("could not initialize vs1053");
        return false;
    }
    _vs1053->begin();
    _vs1053->switchToMp3Mode();
    if (_vs1053->getChipVersion() == 4)
        _vs1053->loadDefaultVs1053Patches();
    setVolume(_volume);
    return true;
}

bool ESP32_VS1053_Stream::isChipConnected() {
    return _vs1053 ? _vs1053->isChipConnected() : false;
}

bool ESP32_VS1053_Stream::connecttohost(const String& url) {
    if (!_vs1053 || _http || !networkIsActive() || !url.startsWith("http"))
        return false;

    _http = new HTTPClient;

    if (!_http) {
        log_e("client could not be created");
        return false;
    }

    {
        String escapedUrl = url;
        escapedUrl.replace(" ", "%20");
        log_d("connecting to %s", url.c_str());
        if (!_http->begin(escapedUrl)) {
            log_e("could not connect to %s", url.c_str());
            stopSong();
            return false;
        }
    }

    // add request headers
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "bytes=%zu-", _offset);
    _http->addHeader("Range", buffer);
    _http->addHeader("Icy-MetaData", VS1053_ICY_METADATA ? "1" : "0");
    _http->setAuthorization(_user.c_str(), _pwd.c_str());

    //prepare for response headers
    const char* CONTENT_TYPE = "Content-Type";
    const char* ICY_NAME = "icy-name";
    const char* ICY_METAINT = "icy-metaint";
    const char* ENCODING = "Transfer-Encoding";
    const char* BITRATE = "icy-br";

    const char* header[] = {CONTENT_TYPE, ICY_NAME, ICY_METAINT, ENCODING, BITRATE};
    _http->collectHeaders(header, sizeof(header) / sizeof(char*));

    //_http->setConnectTimeout(url.startsWith("https") ? CONNECT_TIMEOUT_MS_SSL : CONNECT_TIMEOUT_MS); //temporary(?) hacky solution for the post 2.0.0 issue
    _http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    //const unsigned long START_TIME_MS {millis()}; //only used for debug

    const int result = _http->GET();

    log_d("connection made in %lu ms", (unsigned long)millis() - START_TIME_MS);

    switch (result) {
        case 206 : log_d("server can resume");
        case 200 :
            {
                log_d("connected to %s", url.c_str());

                /* check if we opened a playlist and try to parse it */
                if (_http->header(CONTENT_TYPE).startsWith("audio/x-scpls") ||
                        _http->header(CONTENT_TYPE).equals("audio/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/pls+xml") ||
                        _http->header(CONTENT_TYPE).equals("application/vnd.apple.mpegurl")) {
                    log_d("url %s is a playlist", url.c_str());

                    if (_http->getSize() < 6) {
                        log_e("playlist contains no valid url");
                        stopSong();
                        return false;
                    }

                    WiFiClient* stream = _http->getStreamPtr();
                    char playlist[_http->getSize()];
                    stream->readBytes(playlist, _http->getSize());
                    const char* pch = strstr(playlist, "http");
                    if (!pch) {
                        log_e("playlist contains no valid url");
                        stopSong();
                        return false;
                    }
                    else {
                        log_d("playlist reconnects to: %s", pch);
                        stopSong();
                        return connecttohost(pch);
                    }
                }

                else if (_http->header(CONTENT_TYPE).equals("audio/mpeg"))
                    _currentMimetype = MP3;

                else if (_http->header(CONTENT_TYPE).equals("audio/ogg") || _http->header(CONTENT_TYPE).equals("application/ogg"))
                    _currentMimetype = OGG;

                else if (_http->header(CONTENT_TYPE).equals("audio/aac"))
                    _currentMimetype = AAC;

                else if (_http->header(CONTENT_TYPE).equals("audio/aacp"))
                    _currentMimetype = AACP;

                else {
                    log_e("closing - unsupported mimetype: '%s'", _http->header(CONTENT_TYPE).c_str());
                    stopSong();
                    return false;
                }

                log_d("codec %s", currentCodec());

                if (audio_showstation && !_http->header(ICY_NAME).equals(""))
                    audio_showstation(_http->header(ICY_NAME).c_str());

                _remainingBytes = _http->getSize();  // -1 when Server sends no Content-Length header (chunked streams)
                if (_remainingBytes == -1) _offset = 0;
                _chunkedResponse = _http->header(ENCODING).equals("chunked") ? true : false;
                _metaDataStart = _http->header(ICY_METAINT).toInt();
                _musicDataPosition = _metaDataStart ? 0 : -100;
                log_d("metadata interval is %i", _metaDataStart);
                _bitrate = _http->header(BITRATE).toInt();
                log_d("bitrate is %i", _bitrate);
                _url = url;
                _user.clear();
                _pwd.clear();
                return true;
            }
        default :
            {
                log_e("error %i %s", result, _http->errorToString(result));
                stopSong();
                return false;
            }
    }
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const size_t offset) {
    _offset = offset;
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const String& user, const String& pwd) {
    _user = user;
    _pwd = pwd;
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const String& user, const String& pwd, const size_t offset) {
    _user = user;
    _pwd = pwd;
    _offset = offset;
    return connecttohost(url);
}

static void parseMetadata(char* data, const size_t len) {
    log_d("parsing metadata: %s", data);
    char* pch = strstr(data, "StreamTitle");
    if (pch) pch = strstr(pch, "'");
    else return;
    char* index = ++pch;
    while (index[0] != '\'' || index[1] != ';')
        if (index++ == data + len) return;
    index[0] = 0;
    audio_showstreamtitle(pch);
}

static void _handleStream(WiFiClient* const stream) {
    if (!_dataSeen) {
        log_d("first data bytes are seen - %i bytes", stream->available());
        _dataSeen = true;
        _startMute = millis();
        _startMute += _startMute ? 0 : 1;
        _vs1053->setVolume(0);
        _vs1053->startSong();
    }

    size_t bytesToDecoder = 0;
    while (stream->available() && _vs1053->data_request() && _remainingBytes && _musicDataPosition < _metaDataStart && bytesToDecoder < MAX_BYTES_PER_LOOP) {
        const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PACKETSIZE));
        _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
        _remainingBytes -= _remainingBytes > 0 ? BYTES_IN_BUFFER : 0;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
        bytesToDecoder += BYTES_IN_BUFFER;
    }
    log_d("%5lu bytes to decoder", bytesToDecoder);

    if (_metaDataStart && _musicDataPosition == _metaDataStart && stream->available()) {
        const auto METALENGTH = stream->read() * 16;
        if (METALENGTH) {
            char data[METALENGTH];
            stream->readBytes(data, METALENGTH);
            if (audio_showstreamtitle) parseMetadata(data, METALENGTH);
        }
        _musicDataPosition = 0;
    }
}

static size_t nextChunkSize(WiFiClient* const stream) {
    constexpr const auto BUFFER_SIZE = 8;
    char buffer[BUFFER_SIZE];
    auto cnt = 0;
    char currentChar = (char)stream->read();
    while (currentChar != '\n' && cnt < BUFFER_SIZE) {
        buffer[cnt++] = currentChar;
        currentChar = (char)stream->read();
    }
    return strtol(buffer, NULL, 16);
}

static bool checkSync(WiFiClient* const stream) {
    if ((char)stream->read() != '\r' || (char)stream->read() != '\n') {
        log_e("Lost sync!");
        return false;
    }
    return true;
}


static void _handleChunkedStream(WiFiClient* const stream) {
    if (!_bytesLeftInChunk) {
        _bytesLeftInChunk = nextChunkSize(stream);

        if (!_bytesLeftInChunk) {
            _remainingBytes = 0;
            return;
        }

        if (!_dataSeen) {
            log_d("first data chunk: %i bytes", _bytesLeftInChunk);
            _dataSeen = true;
            _startMute = millis();
            _startMute += _startMute ? 0 : 1;
            _vs1053->setVolume(0);
            _vs1053->startSong();
        }
    }

    size_t bytesToDecoder = 0;
    while (_bytesLeftInChunk && _vs1053->data_request() && _musicDataPosition < _metaDataStart && bytesToDecoder < MAX_BYTES_PER_LOOP) {
        const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, (size_t)_metaDataStart - _musicDataPosition);
        const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PACKETSIZE));
        _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
        _bytesLeftInChunk -= BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
        bytesToDecoder += BYTES_IN_BUFFER;
    }
    log_d("%5lu bytes to decoder", bytesToDecoder);

    if (_metaDataStart && _musicDataPosition == _metaDataStart && _bytesLeftInChunk) {
        const auto METALENGTH = stream->read() * 16;
        _bytesLeftInChunk--;
        if (METALENGTH) {
            char data[METALENGTH];
            auto cnt = 0;
            while (cnt < METALENGTH) {
                if (!_bytesLeftInChunk) {
                    checkSync(stream);
                    _bytesLeftInChunk = nextChunkSize(stream);
                }
                data[cnt++] = stream->read();
                _bytesLeftInChunk--;
            }
            if (audio_showstreamtitle) parseMetadata(data, METALENGTH);
        }
        _musicDataPosition = 0;
    }

    if (!_bytesLeftInChunk)
        checkSync(stream);
}

void ESP32_VS1053_Stream::loop() {
    if (!_http || !_http->connected()) return;

    WiFiClient* const stream = _http->getStreamPtr();
    if (!stream->available()) {
        log_d("No data in HTTP buffer");
        return;
    }

    if (_startMute) {
        const auto WAIT_TIME_MS = (!_bitrate && _remainingBytes == -1) ? 180 : 60;
        if ((unsigned long)millis() - _startMute > WAIT_TIME_MS) {
            _vs1053->setVolume(_volume);
            log_d("startmute ms: %i", WAIT_TIME_MS);
            _startMute = 0;
        }
    }

    if (_remainingBytes && _vs1053->data_request()) {
        if (_chunkedResponse) _handleChunkedStream(stream);
        else _handleStream(stream);
    }

    if (!_remainingBytes) {
        log_d("All data read - closing stream");
        if (audio_eof_stream) {
            char tmp[_url.length()];
            snprintf(tmp, sizeof(tmp), "%s", _url.c_str());
            stopSong();
            audio_eof_stream(tmp);
        }
        else
            stopSong();
    }
}

bool ESP32_VS1053_Stream::isRunning() {
    return _http != NULL;
}

void ESP32_VS1053_Stream::stopSong() {
    if (_http) {
        if (_http->connected()) {
            WiFiClient* const stream = _http->getStreamPtr();
            stream->stop();
            //stream->flush(); // it seems that since 2.0.0 stream->flush() is not needed anymore after a stream->close();
        }
        _http->end();
        delete _http;
        _http = NULL;
        _vs1053->stopSong();
        _dataSeen = false;
        _bufferFilled = false;
        _remainingBytes = 0;
        _bytesLeftInChunk = 0;
        _currentMimetype = STOPPED;
        _url.clear();
        _bitrate = 0;
        _offset = 0;
    }
}

uint8_t ESP32_VS1053_Stream::getVolume() {
    return _volume;
}

void ESP32_VS1053_Stream::setVolume(const uint8_t vol) {
    _volume = vol;
    if (_vs1053 && !_startMute) _vs1053->setVolume(_volume);
}

void ESP32_VS1053_Stream::setTone(uint8_t *rtone) {
    if (_vs1053) _vs1053->setTone(rtone);
}

const char* ESP32_VS1053_Stream::currentCodec() {
    return _mimestr[_currentMimetype];
}

size_t ESP32_VS1053_Stream::size() {
    return _offset + (_http ? _http->getSize() != -1 ? _http->getSize() : 0 : 0);
}

size_t ESP32_VS1053_Stream::position() {
    return size() - _remainingBytes;
}

String ESP32_VS1053_Stream::lastUrl() {
    return _url;
}

uint32_t ESP32_VS1053_Stream::bitrate() {
    return _bitrate;
}
