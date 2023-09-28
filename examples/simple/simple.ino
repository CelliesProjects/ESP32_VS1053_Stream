#include <Arduino.h>
#include <VS1053.h>               /* https://github.com/baldram/ESP_VS1053_Library */
#include <ESP32_VS1053_Stream.h>

#define SPI_CLK_PIN 18
#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23

#define VS1053_CS 5
#define VS1053_DCS 21
#define VS1053_DREQ 22

ESP32_VS1053_Stream stream;

const char* SSID = "xxx";
const char* PSK = "xxx";

void setup() {
#if defined(CONFIG_IDF_TARGET_ESP32S2) && ARDUHAL_LOG_LEVEL != ARDUHAL_LOG_LEVEL_NONE
    delay(3000);
    Serial.setDebugOutput(true);
#endif

    Serial.begin(115200);

    WiFi.begin(SSID, PSK);

    WiFi.setSleep(false); 
    /* important to set this right! See issue #15 */
    
    Serial.println("\n\nSimple vs1053 Streaming example.");

    while (!WiFi.isConnected())
        delay(10);
    Serial.println("wifi connected - starting decoder");

    SPI.setHwCs(true);
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);  /* start SPI before starting decoder */

    if (!stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !stream.isChipConnected())
    {
        Serial.println("Decoder not running");
        while (1) delay(100);
    };

    Serial.println("decoder running - starting stream");

    stream.connecttohost("http://espace.tekno1.fr/tekno1.m3u");

    Serial.print("codec: ");
    Serial.println(stream.currentCodec());

    Serial.print("bitrate: ");
    Serial.print(stream.bitrate());
    Serial.println("kbps");

}

void loop() {
    stream.loop();
    //Serial.printf("Buffer status: %s\n", stream.bufferStatus());
    delay(5);
}

void audio_showstation(const char* info) {
    Serial.printf("showstation: %s\n", info);
}

void audio_showstreamtitle(const char* info) {
    Serial.printf("streamtitle: %s\n", info);
}

void audio_eof_stream(const char* info) {
    Serial.printf("eof: %s\n", info);
}
