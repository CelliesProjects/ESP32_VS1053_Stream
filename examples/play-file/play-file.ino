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

ESP32_VS1053_Stream audio;

// Called when codec is detected
void codecCallBack(const char *codec)
{
    Serial.printf("codec: %s\n", codec);
}

// Called when bitrate is detected (cbr) and changes (vbr)
void bitrateCallback(uint32_t bitrate)
{
    Serial.printf("bitrate: %lu kbps\n", bitrate);
}

// Called on errors
void errorCallback(const char *error)
{
    Serial.printf("error: %s\n", error);
}

// Called on end-of-file
void eofCallback(const char *url)
{
    Serial.printf("eof: %s\n", url);
}

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
    Serial.println("\n\nVS1053 SD Card Playback Example\n");

    // Start SPI bus
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Mount SD card
    if (!mountSDcard()) 
        Serial.println("SD card not mounted");
    
    Serial.println("Starting decoder...");

    // Initialize the VS1053 decoder
    if (!audio.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !audio.isChipConnected()) 
        Serial.println("Decoder not running");

    // Set the codec callback
    audio.setCodecCB(codecCallBack);

    // Set the bitrate callback
    audio.setBitrateCB(bitrateCallback);

    // Set the error callback
    audio.setErrorCB(errorCallback);     

    // Set the EOF callback
    audio.setEofCB(eofCallback);

    Serial.println("Starting SD playback");

    // Start playback from an SD file
    audio.connectToFile(SD, "/track1.mp3");

    if (!audio.isRunning())
        Serial.println("No audio running");
}

void loop() {
    audio.loop();
    delay(5);
}
