#ifndef __ESP32_VS1053_Stream__
#define __ESP32_VS1053_Stream__

// Since ESP32 Arduino Core 2.0.3 logging does not work without the following code
// see: https://github.com/espressif/arduino-esp32/issues/6083
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "esp32_vs1053_stream";
#endif

#include <Arduino.h>
#include <HTTPClient.h>
#include <VS1053.h>  /* https://github.com/baldram/ESP_VS1053_Library */

#define VS1053_INITIALVOLUME          95
#define VS1053_MAXVOLUME              100
#define VS1053_ICY_METADATA           true

#define CONNECT_TIMEOUT_MS            250
#define CONNECT_TIMEOUT_MS_SSL        2500

#define VS1053_USE_HTTP_BUFFER        false                       // if set to false - decoding starts as soon as data is received
                                                                  // if set to true - see VS1053_HTTP_BUFFERSIZE and VS1053_MAX_RETRIES

#define VS1053_HTTP_BUFFERSIZE        ((size_t)1024 * 4)          // on stream start - try to wait for this amount of bytes in the buffer

#define VS1053_MAX_RETRIES            5                           // but just start decoding after MAX_RETRIES regardless of stored amount

#define VS1053_MAX_METADATA_LENGTH    255

#define VS1053_RESUME                 true

const size_t VS1053_PACKETSIZE = 32;

extern void audio_showstation(const char*) __attribute__((weak));
extern void audio_eof_stream(const char*) __attribute__((weak));
extern void audio_showstreamtitle(const char*) __attribute__((weak));


class ESP32_VS1053_Stream {

    public:
        ESP32_VS1053_Stream();
        ~ESP32_VS1053_Stream();

        bool startDecoder(const uint8_t CS, const uint8_t DCS, const uint8_t DREQ);

        bool connecttohost(const String& url);
        bool connecttohost(const String& url, const size_t startrange);
        bool connecttohost(const String& url, const String& user, const String& pwd);
        bool connecttohost(const String& url, const String& user, const String& pwd, const size_t startrange);

        void loop();
        bool isRunning();
        void stopSong(const bool resume=false);
        uint8_t getVolume();
        void setVolume(const uint8_t vol); /* 0-100 */
        String currentCodec();
        size_t size();
        size_t position();
        String lastUrl();
        uint32_t bitrate();

    private:
        VS1053* _vs1053 = NULL;
        void _handleStream(WiFiClient* const stream);
        void _handleChunkedStream(WiFiClient* const stream);
};

#endif
