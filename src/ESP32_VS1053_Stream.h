#ifndef __ESP32_VS1053_Stream__
#define __ESP32_VS1053_Stream__

#include <Arduino.h>
#include <HTTPClient.h>
#include <VS1053.h>  /* https://github.com/baldram/ESP_VS1053_Library */

#define VS1053_INITIALVOLUME          95
#define VS1053_MAXVOLUME              100
#define VS1053_ICY_METADATA           true

#define CONNECT_TIMEOUT_MS            250
#define CONNECT_TIMEOUT_MS_SSL        2500

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
        void setTone(uint8_t *rtone);
        /*  Bass/Treble: void setTone(uint8_t *rtone);
            toneha       = <0..15>        // Setting treble gain (0 off, 1.5dB steps)
            tonehf       = <0..15>        // Setting treble frequency lower limit x 1000 Hz
            tonela       = <0..15>        // Setting bass gain (0 = off, 1dB steps)
            tonelf       = <0..15>        // Setting bass frequency lower limit x 10 Hz
            e.g. uint8_t rtone[4]  = {12, 15, 15, 15}; // initialize bass & treble
            See https://www.vlsi.fi/fileadmin/datasheets/vs1053.pdf section 9.6.3 */
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
