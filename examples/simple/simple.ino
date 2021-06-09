#include <ESP32_VS1053_Stream.h>

#define VS1053_CS     5
#define VS1053_DCS    21
#define VS1053_DREQ   22

ESP32_VS1053_Stream stream;

const char* SSID = "xxx";
const char* PSK = "xxx";

void setup() {
    Serial.begin(115200);

    WiFi.begin(SSID, PSK);

    Serial.println("\n\nSimple vs1053 Streaming example.");

    while (!WiFi.isConnected())
        delay(10);

    Serial.println("connected - starting stream");

    SPI.begin();  /* start SPI before starting decoder */

    stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ);

    stream.connecttohost("http://icecast.omroep.nl/radio6-bb-mp3");
    Serial.print("codec:");
    Serial.println(stream.currentCodec());
}

void loop() {
    stream.loop();
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
