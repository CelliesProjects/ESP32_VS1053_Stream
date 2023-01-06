#include "ESP32_VS1053_Stream.h"

#include <string>

static VS1053* _vs1053 = NULL;
static HTTPClient* _http = NULL;
static char _url[VS1053_MAX_URL_LENGTH];
static char _user[50];
static char _pwd[50];
static unsigned long _startMute = 0;
static size_t _offset = 0;
static size_t _remainingBytes = 0;
static size_t _bytesLeftInChunk = 0;
static int32_t _metaDataStart = 0;
static int32_t _musicDataPosition = 0;
static uint8_t _volume = VS1053_INITIALVOLUME;
static int _bitrate = 0;
static bool _chunkedResponse = false;
static bool _bufferFilled = false;
static bool _dataSeen = false;

static uint8_t _vs1053Buffer[VS1053_PACKETSIZE];
static const auto MAX_BYTES_PER_LOOP = 16 * 1024;

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

bool ESP32_VS1053_Stream::connecttohost(const char* url) {
    if (!_vs1053 || _http || !networkIsActive())
        return false;

    if (tolower(url[0]) != 'h' ||
        tolower(url[1]) != 't' ||
        tolower(url[2]) != 't' ||
        tolower(url[3]) != 'p' )
        return false;

    _http = new HTTPClient;

    if (!_http) {
        log_e("client could not be created");
        return false;
    }

    {
        auto cnt = 0;
        auto index = 0;
        while (index < strlen(url))
            cnt += (url[index++] == ' ') ? 1 : 0;
        char escapedUrl[cnt ? strlen(url) + (3 * cnt) + 1 : 0];
        if (cnt) {
            auto in = 0;
            auto out = 0;
            while (in < strlen(url)) {
                if (url[in] == ' ') {
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

        if (!_http->begin(cnt ? escapedUrl : url)) {
            log_e("could not connect to %s", url);
            stopSong();
            return false;
        }
    }

    // add request headers
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "bytes=%zu-", _offset);
    _http->addHeader("Range", buffer);
    _http->addHeader("Icy-MetaData", VS1053_ICY_METADATA ? "1" : "0");
    _http->setAuthorization(_user, _pwd);

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
                log_d("connected to %s", _url);

                /* check if we opened a playlist and try to parse it */
                if (_http->header(CONTENT_TYPE).startsWith("audio/x-scpls") ||
                        _http->header(CONTENT_TYPE).equals("audio/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/pls+xml") ||
                        _http->header(CONTENT_TYPE).equals("application/vnd.apple.mpegurl")) {
                    log_d("url %s is a playlist", url.c_str());

                    WiFiClient* stream = _http->getStreamPtr();
                    const auto BYTES_TO_READ = min(stream->available(), VS1053_MAX_PLAYLIST_READ);
                    if (!BYTES_TO_READ) {
                        log_e("playlist contains no data");
                        stopSong();
                        return false;
                    }
                    char file[BYTES_TO_READ];
                    stream->readBytes(file, BYTES_TO_READ);
                    char* url = strstr(file, "http");
                    if (!url) {
                        log_e("playlist contains no 'http'");
                        stopSong();
                        return false;
                    }
                    strtok(url, "\n;?");
                    log_d("playlist reconnects to: %s", url);
                    stopSong();
                    return connecttohost(url);
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
                snprintf(_url, sizeof(url), "%s", url);
                return true;
            }
        default :
            {
                log_e("error %i %s", result, _http->errorToString(result).c_str());
                stopSong();
                return false;
            }
    }
}

bool ESP32_VS1053_Stream::connecttohost(const char* url, const size_t offset) {
    _offset = offset;
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const char* url, const char* user, const char* pwd) {
    snprintf(_user, sizeof(_user), user);
    snprintf(_pwd, sizeof(_pwd), pwd);
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const char* url, const char* user, const char* pwd, const size_t offset) {
    snprintf(_user, sizeof(_user), user);
    snprintf(_pwd, sizeof(_pwd), pwd);
    _offset = offset;
    return connecttohost(url);
}

static void handleMetadata(char* data, const size_t len) {
    log_d("parsing metadata: %s", data);
    char* pch = strstr(data, "StreamTitle");
    if (!pch) return;
    pch = strstr(pch, "'");
    if (!pch) return;
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
            if (audio_showstreamtitle) handleMetadata(data, METALENGTH);
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
            if (audio_showstreamtitle) handleMetadata(data, METALENGTH);
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
        const auto WAIT_TIME_MS = ((!_bitrate && _remainingBytes == -1) || _currentMimetype == AAC || _currentMimetype == AACP) ? 380 : 80;
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
            char tmp[strlen(_url) + 1];
            snprintf(tmp, sizeof(tmp), "%s", _url);
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
        _url[0] = 0;
        _user[0] = 0;
        _pwd[0] = 0;
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

const char* ESP32_VS1053_Stream::lastUrl() {
    return _url;
}

size_t ESP32_VS1053_Stream::size() {
    return _offset + (_http ? _http->getSize() != -1 ? _http->getSize() : 0 : 0);
}

size_t ESP32_VS1053_Stream::position() {
    return size() - _remainingBytes;
}

uint32_t ESP32_VS1053_Stream::bitrate() {
    return _bitrate;
}
