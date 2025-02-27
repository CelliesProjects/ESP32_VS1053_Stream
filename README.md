# ESP32_VS1053_Stream

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/7571166c872e4dc8a899382389b73f8e)](https://app.codacy.com/gh/CelliesProjects/ESP32_VS1053_Stream?utm_source=github.com&utm_medium=referral&utm_content=CelliesProjects/ESP32_VS1053_Stream&utm_campaign=Badge_Grade_Settings)

A streaming library for esp32, esp32-wrover, esp32-c3, esp32-s2 and esp32-s3 with a separate VS1053 codec chip.<br>
This library plays mp3, ogg, aac, aac+ and <strike>flac</strike> files and streams and uses [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) to communicate with the decoder.

Supported stream methods are http and insecure https. Streams can be chunked.<br>
Also plays mp3 and ogg files from sdcard or any mounted filesystem.

## How to install and use

Install [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) and this library in your Arduino library folder.

Take care to install the master branch of the VS1053 library or at least a version from commit [ba1803f](https://github.com/baldram/ESP_VS1053_Library/commit/ba1803f75722a36f3e9f539129e885bea3c60f71) or later because the `getChipVersion()` call that is needed is not included in the latest release.<br>See https://github.com/CelliesProjects/ESP32_VS1053_Stream/issues/23

Use the [latest Arduino ESP32 core version](https://github.com/espressif/arduino-esp32/releases/latest).

## Example: play a stream
```c++
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

void setup() {
    Serial.begin(115200);

    while (!Serial)
        delay(10);

    Serial.println("\n\nVS1053 Radio Streaming Example\n");

    // Connect to Wi-Fi
    Serial.printf("Connecting to WiFi network: %s\n", SSID);
    WiFi.begin(SSID, PSK);  
    WiFi.setSleep(false);  // Important to disable sleep to ensure stable connection

    while (!WiFi.isConnected())
        delay(10);

    Serial.println("WiFi connected - starting decoder...");

    // Start SPI bus
    SPI.setHwCs(true);
    SPI.begin(SPI_CLK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

    // Initialize the VS1053 decoder
    if (!stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ) || !stream.isChipConnected()) {
        Serial.println("Decoder not running - system halted");
        while (1) delay(100);
    }
    Serial.println("VS1053 running - starting radio stream");

    // Connect to the radio stream
    stream.connecttohost("http://icecast.omroep.nl/radio6-bb-mp3");

    if (!stream.isRunning()) {
        Serial.println("Stream not running - system halted");
        while (1) delay(100);
    }

    Serial.print("Codec: ");
    Serial.println(stream.currentCodec());

    Serial.print("Bitrate: ");
    Serial.print(stream.bitrate());
    Serial.println(" kbps");
}

void loop() {
    stream.loop();
    delay(5);
}

void audio_showstation(const char* info) {
    Serial.printf("Station: %s\n", info);
}

void audio_showstreamtitle(const char* info) {
    Serial.printf("Stream title: %s\n", info);
}

void audio_eof_stream(const char* info) {
    Serial.printf("End of stream: %s\n", info);
}
```

## Example: play from SD card
```c++
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
```

## Known issues
Ogg files can not be started with an offset without first playing a couple of seconds from the start of the file. 

## Tips for troublefree streaming

### WiFi setup

Do not forget to switch WiFi out of power save mode:

```c++
...
WiFi.begin(SSID, PSK);
WiFi.setSleep(false); 
...
```

### Prevent reboots while playing
Early version of the esp32 have issues with the external psram cache, resulting in reboots.<br>Workarounds are possible depending on the hardware revision.

#### Revision V0.0
No workarounds are possible for this revision other than not using the psram.

#### Revision V1.0
On revision V1.0 psram can be used with the following build flags:
```bash
-D BOARD_HAS_PSRAM
-mfix-esp32-psram-cache-issue
-mfix-esp32-psram-cache-strategy=memw
```

#### Revision V3.0
On revision V3.0 psram can be used with the following build flag:
```bash
-D BOARD_HAS_PSRAM
```

Source: [esp-idf api guide on external ram](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html#chip-revisions).

#### Find your hardware revision

In PIO you can find out what hardware revision you have by running `esptool.py flash_id` in a terminal.

In Arduino IDE go to `File->Preferences` and find the `Show verbose output during` option. Check the box marked `upload`.<br>You can now see the hardware revision when you upload a sketch.

# Functions
### Initialize the VS1053 codec

```c++
bool startDecoder(CS, DCS, DREQ)
```
### Check if VS1053 is responding
```c++
bool isChipConnected()
```
### Start or resume a stream
```c++
bool connecttohost(url)
```
```c++
bool connecttohost(url, offset)
```
```c++
bool connecttohost(url, user, pwd)
```
```c++
bool connecttohost(url, user, pwd, offset)
```
Note: When a stream does not start in this library but it does play on your desktop or laptop you can try increasing the connection timeout.<br>
You can do this in 
`ESP32_VS1053_Stream.h` by increasing these values:<br>
```c++
#define VS1053_CONNECT_TIMEOUT_MS 250
#define VS1053_CONNECT_TIMEOUT_MS_SSL 750
```
### Start or resume a local file
```c++
bool connecttofile(filesystem, filename)
```
```c++
bool connecttofile(filesystem, filename, offset)
```
`filesystem` has to be mounted.
### Stop a running stream
```c++
void stopSong()
```
### Feed the decoder
```c++
void loop()
```
This function has to called every couple of ms to feed the decoder with data.<br>For bitrates up to 320kbps somewhere between 5-25 ms is about right.
### Check if stream is running
```c++
bool isRunning()
```
### Get the current volume
```c++
uint8_t getVolume()
```
### Set the volume
```c++
void setVolume(newVolume)
```
`newVolume` should be in the range 0-100.
### Set bass and treble
```c++
uint8_t rtone[4]  = {toneha, tonehf, tonela, tonelf};
void setTone(rtone)
```
Values for `rtone`:
```c++
toneha       = <0..15>        // Setting treble gain (0 off, 1.5dB steps)
tonehf       = <0..15>        // Setting treble frequency lower limit x 1000 Hz
tonela       = <0..15>        // Setting bass gain (0 = off, 1dB steps)
tonelf       = <0..15>        // Setting bass frequency lower limit x 10 Hz
```
### Get the current used codec
```c++
const char* currentCodec()
```
Returns `STOPPED` if no stream is running.
### Get the current stream url
```c++
const char* lastUrl()
```
The current stream url might differ from the request url if the request url points to a playlist.
### Get the filesize
```c++
size_t size()
```
Returns `0` if the stream is a radio stream.
### Get the current position in the file
```c++
size_t position()
```
Returns `0` if the stream is a radio stream.
### Get the buffer fill status
```c++
const char *bufferStatus()
```
Returns `0/0` if there is no buffer.<br>Otherwise returns something like `4096/65536` which means 4kB waiting in a 64kB buffer.
```c++
void bufferStatus(size_t &used, size_t &capacity)
```
This version takes two `size_t` variables by reference.<br>Works the same as the `const char *` version.

NOTE: A buffer will only be allocated if there is enough free psram.

# Event callbacks

### Station name callback.

```c++
void audio_showstation(const char* info)
```
### Stream information callback.

```c++
void audio_showstreamtitle(const char* info)
```
### End of file callback.

```c++
void audio_eof_stream(const char* info)
```
Returns the eof url or path.<br>Also called if a stream or file times out/errors.

You can use this function for coding a playlist.<br>Use `connecttohost()` or `connecttofile()` inside this function to start the next item.
## License

MIT License

Copyright (c) 2021 Cellie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
