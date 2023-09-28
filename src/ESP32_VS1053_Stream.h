#ifndef __ESP32_VS1053_Stream__
#define __ESP32_VS1053_Stream__

#include <Arduino.h>
#include <HTTPClient.h>
#include <freertos/ringbuf.h>
#include <esp_heap_caps.h>
#include <VS1053.h> /* https://github.com/baldram/ESP_VS1053_Library */

#define VS1053_INITIALVOLUME 95
#define VS1053_ICY_METADATA true
#define VS1053_CONNECT_TIMEOUT_MS 250
#define VS1053_CONNECT_TIMEOUT_MS_SSL 2500
#define VS1053_DATA_TIMEOUT_MS 900
#define VS1053_MAX_PLAYLIST_READ 1024
#define VS1053_MAX_URL_LENGTH 512
#define VS1053_MAX_REDIRECT_COUNT 3

#define VS1053_PSRAM_BUFFER_ENABLED true
#define VS1053_PSRAM_BUFFER_SIZE size_t(1024 * 64)
#define VS1053_PSRAM_MAX_MOVE size_t(1024 * 4)

#define VS1053_MAXVOLUME uint8_t(100)     /* do not change */
#define VS1053_PLAYBUFFER_SIZE size_t(32) /* do not change */

extern void audio_showstation(const char *) __attribute__((weak));
extern void audio_eof_stream(const char *) __attribute__((weak));
extern void audio_showstreamtitle(const char *) __attribute__((weak));

class ESP32_VS1053_Stream
{

public:
    ESP32_VS1053_Stream();
    ~ESP32_VS1053_Stream();

    bool startDecoder(const uint8_t CS, const uint8_t DCS, const uint8_t DREQ);
    bool isChipConnected();

    bool connecttohost(const char *url);
    bool connecttohost(const char *url, const size_t offset);
    bool connecttohost(const char *url, const char *username, const char *pwd);
    bool connecttohost(const char *url, const char *username, const char *pwd, const size_t offset);

    void loop();
    bool isRunning();
    void stopSong();
    uint8_t getVolume();
    void setVolume(const uint8_t newVolume); /* 0-100 */
    void setTone(uint8_t *rtone);
    /*  Bass/Treble: void setTone(uint8_t *rtone);
        toneha       = <0..15>        // Setting treble gain (0 off, 1.5dB steps)
        tonehf       = <0..15>        // Setting treble frequency lower limit x 1000 Hz
        tonela       = <0..15>        // Setting bass gain (0 = off, 1dB steps)
        tonelf       = <0..15>        // Setting bass frequency lower limit x 10 Hz
        e.g. uint8_t rtone[4]  = {12, 15, 15, 15}; // initialize bass & treble
        See https://www.vlsi.fi/fileadmin/datasheets/vs1053.pdf section 9.6.3 */
    const char *currentCodec();
    const char *lastUrl();
    size_t size();
    size_t position();
    uint32_t bitrate();
    const char *bufferStatus();

private:
    VS1053 *_vs1053;
    HTTPClient *_http;
    uint8_t _vs1053Buffer[VS1053_PLAYBUFFER_SIZE];
    uint8_t _localbuffer[VS1053_PSRAM_MAX_MOVE];
    char _url[VS1053_MAX_URL_LENGTH];
    char _savedStartChar = 0;

    RingbufHandle_t _ringbuffer_handle;
    StaticRingbuffer_t *_buffer_struct;
    uint8_t *_buffer_storage;

    size_t _nextChunkSize(WiFiClient *const stream);
    bool _checkSync(WiFiClient *const stream);
    void _handleMetadata(char *data, const size_t len);
    void _eofStream();
    bool _networkIsActive();
    bool _canRedirect();
    void _handleStream(WiFiClient *const stream);
    void _handleChunkedStream(WiFiClient *const stream);
    void _allocateRingbuffer();
    void _deallocateRingbuffer();
    void _playFromRingBuffer();
    void _streamToRingBuffer(WiFiClient *const stream);
    void _chunkedStreamToRingBuffer(WiFiClient *const stream);

    unsigned long _startMute = 0;
    size_t _offset = 0;
    int32_t _remainingBytes = 0;
    size_t _bytesLeftInChunk = 0;
    int32_t _metaDataStart = 0;
    int32_t _musicDataPosition = 0;
    uint8_t _volume = VS1053_INITIALVOLUME;
    int _bitrate = 0;
    bool _chunkedResponse = false;
    bool _dataSeen = false;
    bool _ringbuffer_filled = false;
    unsigned long _streamStalledTime = 0;
    uint8_t _redirectCount = 0;

    enum codec_t
    {
        STOPPED,
        MP3,
        OGG,
        AAC,
        AACP
    } _currentCodec = STOPPED;
};

#endif
