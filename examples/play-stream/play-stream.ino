#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <VS1053.h>               // https://github.com/baldram/ESP_VS1053_Library
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

// Called when a stream has an ICY name header set
void stationCallback(const char *name)
{
    Serial.printf("station: %s\n", name);
}

// Called when stream metadata is available
void infoCallback(const char *info)
{
    Serial.printf("info: %s\n", info);
}

// Called on stream errors
void errorCallback(const char *error)
{
    Serial.printf("error: %s\n", error);
}

// Called on end-of-file
void eofCallback(const char *url)
{
    Serial.printf("eof: %s\n", url);
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nVS1053 Radio Streaming Example\n");

    // Connect to Wi-Fi
    Serial.printf("Connecting to WiFi network: %s\n", SSID);
    WiFi.begin(SSID, PSK);  
    WiFi.setSleep(false);  // Important to disable sleep to ensure stable connection

    while (!WiFi.isConnected())
        delay(10);

    Serial.println("WiFi connected");

    // Start SPI bus
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Initialize the VS1053 decoder
    if (!stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !stream.isChipConnected())
        Serial.println("Decoder not running");

    // Set the codec callback
    stream.setCodecCB(codecCallBack);

    // Set the bitrate callback
    stream.setBitrateCB(bitrateCallback);   

    // Set the station name callback
    stream.setStationCB(stationCallback);

    // Set the stream metadata callback
    stream.setInfoCB(infoCallback);

    // Set the error callback
    stream.setErrorCB(errorCallback); 

    // Set the EOF callback
    stream.setEofCB(eofCallback);    

    Serial.println("Starting radio stream");

    // Connect to the radio stream
    stream.connectToHost("http://icecast.omroep.nl/radio6-bb-mp3");

    if (!stream.isRunning())
        Serial.println("Stream not running");
}

void loop() {
    stream.loop();
    delay(5);
}
