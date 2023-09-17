# ESP32_VS1053_Stream

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/7571166c872e4dc8a899382389b73f8e)](https://app.codacy.com/gh/CelliesProjects/ESP32_VS1053_Stream?utm_source=github.com&utm_medium=referral&utm_content=CelliesProjects/ESP32_VS1053_Stream&utm_campaign=Badge_Grade_Settings)

A streaming library for esp32, esp32-wrover, esp32-s2 and esp32-s3 with a separate VS1053 mp3/ogg/aac/flac/wav decoder.<br>
This library plays mp3, ogg, aac, aac+ and <strike>flac</strike> files and streams. Supports http, https (insecure mode) and chunked audio streams.

This library needs [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) to communicate with the decoder.

Visit [eStreamPlayer32_VS1053 for PIO](https://github.com/CelliesProjects/eStreamplayer32-vs1053-pio) to see a [PlatformIO](https://platformio.org/platformio) project using this library.

## How to install and use

Install [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) and this library in your Arduino library folder.

Use [the latest Arduino ESP32 core version](https://github.com/espressif/arduino-esp32/releases/latest).

## Example code

```c++
#include <VS1053.h>               /* https://github.com/baldram/ESP_VS1053_Library */
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
    Serial.println("wifi connected - starting decoder");

    SPI.begin();  /* start SPI before starting decoder */

    stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ);

    Serial.println("decoder running - starting stream");

    stream.connecttohost("http://icecast.omroep.nl/radio6-bb-mp3");

    Serial.print("codec: ");
    Serial.println(stream.currentCodec());

    Serial.print("bitrate: ");
    Serial.print(stream.bitrate());
    Serial.println("kbps");

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
```

# Functions

### Initialize the VS1053 codec

```c++
bool startDecoder(CS, DCS, DREQ)
```

<hr>

### Check if VS1053 is responding

```c++
bool isChipConnected()
```

<hr>

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

<hr>

### Stop a stream

```c++
void stopSong()
```

<hr>

### Feed the decoder

```c++
void loop()
```

This function has to called every couple of ms to feed the decoder with data. For bitrates up to 320kbps once every 25 ms is about right.

<hr>

### Check if stream is running

```c++
bool isRunning()
```

<hr>

### Get the current volume

```c++
uint8_t getVolume()
```

<hr>

### Set the volume

```c++
void setVolume(uint8_t volume)
```

Value should be between 0-100.

<hr>

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

<hr>

### Get the current used codec

```c++
const char* currentCodec()
```

Returns `STOPPED` if no stream is running.

<hr>

### Get the current stream url

```c++
const char* lastUrl()
```

The current stream url might differ from the request url if the request url points to a playlist.

<hr>

### Get the filesize

```c++
size_t size()
```

Returns `0` if the stream is a radio stream.

<hr>

### Get the current position in the file

```c++
size_t position()
```

Returns `0` if the stream is a radio stream.

<hr>

# Event callbacks

### Station name callback.

```c++
void audio_showstation(const char* info)
```

<hr>

### Stream information callback.

```c++
void audio_showstreamtitle(const char* info)
```

<hr>

### End of file callback.

```c++
void audio_eof_stream(const char* info)
```

Returns the current url. Very handy for coding a playlist.

<hr>

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
