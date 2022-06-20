#include "ESP32_VS1053_Stream.h"

static HTTPClient* _http = NULL;
static String _url;
static size_t _remainingBytes = 0;
static size_t _startrange = 0;
static String _user;
static String _pwd;
static bool _bufferFilled = false;
static uint8_t _volume = VS1053_INITIALVOLUME;
static int32_t _metaDataStart = 0;
static int32_t _musicDataPosition = 0; /* position within current music data block */
static bool _chunkedResponse = false;
static size_t _bytesLeftInChunk = 0;
static bool _dataSeen = false;
static int _bitrate = 0;
static unsigned long _startMute = 0; /* mutes the sound during stream startup to supress the crack that comes with starting a stream */

static uint8_t _vs1053Buffer[VS1053_PACKETSIZE];

static enum mimetype_t {
    MP3,
    OGG,
    AAC,
    AACP,
    UNKNOWN
} _currentMimetype = UNKNOWN;

static const String mimestr[] = {"MP3", "OGG", "AAC", "AAC+", "UNKNOWN"};

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
        ESP_LOGE(TAG, "vs1053 is already initialized");
        return false;
    }
    _vs1053 = new VS1053(CS, DCS, DREQ);
    if (!_vs1053) {
        ESP_LOGE(TAG, "could not initialize vs1053");
        return false;
    }
    _vs1053->begin();
    _vs1053->loadDefaultVs1053Patches();
    _vs1053->switchToMp3Mode();
    setVolume(_volume);
    return true;
}

bool ESP32_VS1053_Stream::connecttohost(const String& url) {

    if (!_vs1053) {
        ESP_LOGE(TAG, "vs1053 is not initialized");
        return false;
    }

    if (!url.startsWith("http")) {
        ESP_LOGE(TAG, "url should start with http or https");
        return false;
    }

    if (_http) {
        ESP_LOGE(TAG, "client already running!");
        return false;
    }

    if (!networkIsActive()) {
        ESP_LOGE(TAG, "no active network adapter");
        return false;
    }

    _http = new HTTPClient;

    if (!_http) {
        ESP_LOGE(TAG, "client could not be created");
        return false;
    }

    {
        String escapedUrl = url;
        escapedUrl.replace(" ", "%20");
        ESP_LOGD(TAG, "connecting to %s", url.c_str());
        if (!_http->begin(escapedUrl)) {
            ESP_LOGE(TAG, "could not connect to %s", url.c_str());
            stopSong();
            return false;
        }
    }

    // add request headers
    _http->addHeader("Icy-MetaData", VS1053_ICY_METADATA ? "1" : "0");

    if (_startrange)
        _http->addHeader("Range", " bytes=" + String(_startrange) + "-");

    if (_user || _pwd)
        _http->setAuthorization(_user.c_str(), _pwd.c_str());

    //prepare for response headers
    const char* CONTENT_TYPE = "Content-Type";
    const char* ICY_NAME = "icy-name";
    const char* ICY_METAINT = "icy-metaint";
    const char* ENCODING = "Transfer-Encoding";
    const char* BITRATE = "icy-br";

    const char* header[] = {CONTENT_TYPE, ICY_NAME, ICY_METAINT, ENCODING, BITRATE};
    _http->collectHeaders(header, sizeof(header) / sizeof(char*));

    _http->setConnectTimeout(url.startsWith("https") ? CONNECT_TIMEOUT_MS_SSL : CONNECT_TIMEOUT_MS);
    _http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    const unsigned long START_TIME_MS {millis()};

    const int result = _http->GET();

    ESP_LOGD(TAG, "connection made in %lu ms", (unsigned long)millis() - START_TIME_MS);

    switch (result) {
        case 206 : ESP_LOGD(TAG, "server can resume");
        case 200 :
            {
                ESP_LOGD(TAG, "connected to %s", url.c_str());

                /* check if we opened a playlist and try to parse it */
                if (_http->header(CONTENT_TYPE).startsWith("audio/x-scpls") ||
                        _http->header(CONTENT_TYPE).equals("audio/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/x-mpegurl") ||
                        _http->header(CONTENT_TYPE).equals("application/pls+xml") ||
                        _http->header(CONTENT_TYPE).equals("application/vnd.apple.mpegurl")) {
                    ESP_LOGW(TAG, "url is a playlist");

                    const String payload = _http->getString();

                    ESP_LOGD(TAG, "payload: %s", payload.c_str());

                    auto index = payload.indexOf("http");
                    if (-1 == index) {
                        ESP_LOGW(TAG, "no url found in file");
                        stopSong();
                        return false;
                    }

                    String newUrl;
                    while (payload.charAt(index) != '\n' && index < payload.length())
                        newUrl.concat(payload.charAt(index++));

                    newUrl.trim();

                    ESP_LOGW(TAG, "file parsed - reconnecting to: %s", newUrl.c_str());

                    stopSong();
                    return connecttohost(newUrl);
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
                    ESP_LOGE(TAG, "closing - unsupported mimetype %s", _http->header(CONTENT_TYPE).c_str());
                    stopSong();
                    return false;
                }

                ESP_LOGD(TAG, "codec %s", currentCodec());

                if (audio_showstation && !_http->header(ICY_NAME).equals(""))
                    audio_showstation(_http->header(ICY_NAME).c_str());

                _remainingBytes = _http->getSize();  // -1 when Server sends no Content-Length header
                _chunkedResponse = _http->header(ENCODING).equals("chunked") ? true : false;
                _metaDataStart = _http->header(ICY_METAINT).toInt();
                _musicDataPosition = _metaDataStart ? 0 : -100;
                ESP_LOGD(TAG, "metadata interval is %i", _metaDataStart);
                _bitrate = _http->header(BITRATE).toInt();
                ESP_LOGD(TAG, "bitrate is %i", _bitrate);
                _url = url;
                _user.clear();
                _pwd.clear();
                _startrange = 0;
                return true;
            }
        default :
            {
                ESP_LOGE(TAG, "error %i", result);
                stopSong();
                return false;
            }
    }
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const size_t startrange) {
    _startrange = startrange;
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const String& user, const String& pwd) {
    _user = user;
    _pwd = pwd;
    return connecttohost(url);
}

bool ESP32_VS1053_Stream::connecttohost(const String& url, const String& user, const String& pwd, const size_t startrange) {
    _user = user;
    _pwd = pwd;
    _startrange = startrange;
    return connecttohost(url);
}

static void _parseMetaData(const String& data) {
    ESP_LOGD(TAG, "metadata: %s", data.c_str());
    if (audio_showstreamtitle && data.startsWith("StreamTitle")) {
        int32_t pos = data.indexOf("'");
        if (pos != -1) {
            pos++;
            int32_t pos2 = data.indexOf("';", pos);
            pos2 = (pos2 == -1) ? data.length() : pos2;
            String streamtitle;
            while (pos < pos2)
                streamtitle.concat(data.charAt(pos++));

            if (!streamtitle.equals("")) audio_showstreamtitle(streamtitle.c_str());
        }
    }
}

void ESP32_VS1053_Stream::_handleStream(WiFiClient* const stream) {
    if (!_dataSeen && stream->available()) {
        ESP_LOGD(TAG, "first data bytes are seen - %i bytes", stream->available());
        _dataSeen = true;
        _startMute = millis();
        _startMute += _startMute ? 0 : 1;
        _vs1053->setVolume(0);
        _vs1053->startSong();
    }

    //size_t bytesToDecoder = 0; //only used for debug

    while (stream->available() && _vs1053->data_request() && _remainingBytes && _musicDataPosition < _metaDataStart) {
        const size_t BYTES_AVAILABLE = _metaDataStart ? _metaDataStart - _musicDataPosition : stream->available();
        const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PACKETSIZE));
        _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
        _remainingBytes -= _remainingBytes > 0 ? BYTES_IN_BUFFER : 0;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
        //bytesToDecoder += BYTES_IN_BUFFER;
    }

    ESP_LOGD(TAG, "%5lu bytes to decoder", bytesToDecoder);

    if (_metaDataStart && _musicDataPosition == _metaDataStart && stream->available()) {
        int32_t metaLength = stream->read() * 16;
        if (metaLength) {
            String data;
            while (metaLength) {
                const char ch = stream->read();
                data.concat(data.length() < VS1053_MAX_METADATA_LENGTH ? ch : (char){});
                metaLength--;
            }
            if (!data.equals("")) _parseMetaData(data);
        }
        _musicDataPosition = 0;
    }
}

void ESP32_VS1053_Stream::_handleChunkedStream(WiFiClient* const stream) {
    if (!_bytesLeftInChunk) {
        _bytesLeftInChunk = strtol(stream->readStringUntil('\n').c_str(), NULL, 16);
        ESP_LOGD(TAG, "chunk size: %i", _bytesLeftInChunk);

        if (!_dataSeen && _bytesLeftInChunk) {
            ESP_LOGD(TAG, "first data chunk: %i bytes", _bytesLeftInChunk);
            _dataSeen = true;
            _startMute = millis();
            _startMute += _startMute ? 0 : 1;
            _vs1053->setVolume(0);
            _vs1053->startSong();
        }
    }

    //size_t bytesToDecoder = 0; //only used for debug

    while (_bytesLeftInChunk && _vs1053->data_request() && _musicDataPosition < _metaDataStart) {
        const size_t BYTES_AVAILABLE = min(_bytesLeftInChunk, (size_t)_metaDataStart - _musicDataPosition);
        const int BYTES_IN_BUFFER = stream->readBytes(_vs1053Buffer, min(BYTES_AVAILABLE, VS1053_PACKETSIZE));
        _vs1053->playChunk(_vs1053Buffer, BYTES_IN_BUFFER);
        _bytesLeftInChunk -= BYTES_IN_BUFFER;
        _musicDataPosition += _metaDataStart ? BYTES_IN_BUFFER : 0;
        //bytesToDecoder += BYTES_IN_BUFFER;
    }

    ESP_LOGD(TAG, "%5lu bytes to decoder", bytesToDecoder);

    if (_metaDataStart && _musicDataPosition == _metaDataStart && _bytesLeftInChunk) {
        int32_t metaLength = stream->read() * 16;
        _bytesLeftInChunk--;
        if (metaLength) {
            String data;
            while (metaLength) {
                if (!_bytesLeftInChunk) {
                    stream->readStringUntil('\n');
                    while (!stream->available()) delay(1);
                    _bytesLeftInChunk = strtol(stream->readStringUntil('\n').c_str(), NULL, 16);
                }
                const char ch = stream->read();
                data.concat(data.length() < VS1053_MAX_METADATA_LENGTH ? ch : (char){});
                _bytesLeftInChunk--;
                metaLength--;
            }
            if (!data.equals("")) _parseMetaData(data);
        }
        _musicDataPosition = 0;
    }

    if (!_bytesLeftInChunk)
        stream->readStringUntil('\n');
}

void ESP32_VS1053_Stream::loop() {
    if (!_http || !_http->connected()) return;

    WiFiClient* const stream = _http->getStreamPtr();
    if (!stream->available()) return;

#if (VS1053_USE_HTTP_BUFFER == true)
    {
        static auto count = 0;

        if ((!_bufferFilled && count++ < VS1053_MAX_RETRIES) && stream->available() < min(VS1053_HTTP_BUFFERSIZE, _remainingBytes)) {
            ESP_LOGI(TAG, "Pass: %i available: %i", count, stream->available());
            return;
        }

        _bufferFilled = true;
        count = 0;
    }
#endif

    if (_startMute) {
        const auto WAIT_TIME_MS = (!_bitrate && _remainingBytes == -1) ? 180 : 60;
        if ((unsigned long)millis() - _startMute > WAIT_TIME_MS) {
            _vs1053->setVolume(_volume);
            ESP_LOGD(TAG, "startmute ms: %i", WAIT_TIME_MS);
            _startMute = 0;
        }
    }

    if (_remainingBytes && _vs1053->data_request()) {
        if (_chunkedResponse) _handleChunkedStream(stream);
        else _handleStream(stream);
    }

    if (!_remainingBytes) {
        ESP_LOGD(TAG, "all data read - closing stream");
        const String temp = audio_eof_stream ? _url : "";
        stopSong();
        if (audio_eof_stream) audio_eof_stream(temp.c_str());
    }
}

bool ESP32_VS1053_Stream::isRunning() {
    return _http != NULL;
}

void ESP32_VS1053_Stream::stopSong(const bool resume) {
    if (_http) {
        if (_http->connected()) {
            WiFiClient* const stream = _http->getStreamPtr();
            stream->stop();
            stream->flush(); // it seems that since 2.0.0 stream->flush() is not needed anymore after a stream->close();
        }
        _http->end();
        delete _http;
        _http = NULL;

        if (_vs1053 && !resume)
            _vs1053->stopSong();

        _dataSeen = false;
        _bufferFilled = false;
        _remainingBytes = 0;
        _bytesLeftInChunk = 0;
        _currentMimetype = UNKNOWN;
        _url.clear();
        _bitrate = 0;
    }
}

uint8_t ESP32_VS1053_Stream::getVolume() {
    return _volume;
}

void ESP32_VS1053_Stream::setVolume(const uint8_t vol) {
    _volume = vol;
    if (_vs1053 && !_startMute) _vs1053->setVolume(_volume);
}

String ESP32_VS1053_Stream::currentCodec() {
    return mimestr[_currentMimetype];
}

size_t ESP32_VS1053_Stream::size() {
    return _http ? _http->getSize() != -1 ? _http->getSize() : 0 : 0;
}

size_t ESP32_VS1053_Stream::position() {
    return size() - _remainingBytes;
}

String ESP32_VS1053_Stream::lastUrl() {
    return _url;
}

uint32_t ESP32_VS1053_Stream::bitrate(){
    return _bitrate;
}
