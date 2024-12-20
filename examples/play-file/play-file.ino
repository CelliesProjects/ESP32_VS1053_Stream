#include <Arduino.h>
#include <SD.h>
#include <VS1053.h>               // https://github.com/baldram/ESP_VS1053_Library
#include <ESP32_VS1053_Stream.h>

#define SPI_CLK_PIN 18
#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23

#define VS1053_CS 5
#define VS1053_DCS 21
#define VS1053_DREQ 22
#define SDREADER_CS 26

ESP32_VS1053_Stream stream;

bool mountSDcard() {
    if (!SD.begin(SDREADER_CS)) {
        Serial.println("Card mount failed"); 
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached");
        return false;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    return true;
}

void setup() {
    Serial.begin(115200);

    while (!Serial)
        delay(10);

    Serial.println("\n\nVS1053 SD Card Playback Example\n");

    // Start SPI bus
    SPI.setHwCs(true);
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Mount SD card
    if (!mountSDcard()) {
        Serial.println("SD card not mounted - system halted");
        while (1) delay(100);
    }

    Serial.println("SD card mounted - starting decoder...");

    // Initialize the VS1053 decoder
    if (!stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !stream.isChipConnected()) {
        Serial.println("Decoder not running - system halted");
        while (1) delay(100);
    }

    Serial.println("VS1053 running - starting SD playback");

    // Start playback from an SD file
    stream.connecttofile(SD, "/test.mp3");

    if (!stream.isRunning()) {
        Serial.println("No file running - system halted");
        while (1) delay(100);
    }

    Serial.print("Codec: ");
    Serial.println(stream.currentCodec());
}

void loop() {
    stream.loop();
    delay(5);
}

void audio_eof_stream(const char* info) {
    Serial.printf("End of file: %s\n", info);
}
