# ESP32_VS1053_Stream

A streaming library for esp32 with a vs1053 mp3/aac/ogg decoder.

Plays chunked streams and parses some of the metadata.
See `eStreamPlayer32_VS1053` for a project using this library.

This library depends on the [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) to communicate with the decoder.

Install `ESP_VS1053_Library` and `ESP32_VS1053_Stream` in your Arduino library folder.

For now you will have to modify `ESP_VS1053_Library` to be able to load the latest firmware patch. A commit addressing this issue has been opened at the `ESP_VS1053_Library` repo.

## How to modify `ESP_VS1053_Library`

Open `VS1053.h` and move `void write_register(uint8_t _reg, uint16_t _value) const;` from the `private:` section to the `public:` section and save the file.

## Example code

```c++
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

void audio_eof_stream(const char* info) {
    Serial.printf("eof: %s\n", info);
```


