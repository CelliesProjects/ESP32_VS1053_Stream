# ESP32_VS1053_Stream

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/7571166c872e4dc8a899382389b73f8e)](https://app.codacy.com/gh/CelliesProjects/ESP32_VS1053_Stream?utm_source=github.com&utm_medium=referral&utm_content=CelliesProjects/ESP32_VS1053_Stream&utm_campaign=Badge_Grade_Settings)

A streaming library for esp32 with a separate VS1053 mp3/ogg/aac/flac/wav decoder.

This library plays mp3, ogg, aac, aac+ and flac files and streams. 

Supports http, https (insecure mode) and chunked audio streams.

This library needs [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) to communicate with the decoder.

Visit [eStreamPlayer32_VS1053](https://github.com/CelliesProjects/eStreamPlayer32_VS1053) to see a project using this library.

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

## Functions

### Initialize the VS1053 codec
`bool startDecoder(CS, DCS, DREQ)`

### Check if VS1053 is responding
`bool isChipConnected()`<br>

### Start or resume a stream
`bool connecttohost(url)`

`bool connecttohost(url, offset)`

`bool connecttohost(url, user, pwd)`

`bool connecttohost(url, user, pwd, offset)`


### Stop a stream
```void stopSong()```

### Feed the decoder
```void loop()```<br>
This function has to called every couple of ms to feed the decoder with data. For bitrates up to 320kbps once every 25 ms is about right.

### Check if stream is running
```bool isRunning()```

### Get the current volume
```uint8_t getVolume()```

### Set the volume
```void setVolume(uint8_t volume)```<br>
Value should be between 0-100.

### Set bass and treble
```uint8_t rtone[4]  = {toneha, tonehf, tonela, tonelf};```<br>
```void setTone(rtone)```

Values for `rtone`:
```
toneha       = <0..15>        // Setting treble gain (0 off, 1.5dB steps)
tonehf       = <0..15>        // Setting treble frequency lower limit x 1000 Hz
tonela       = <0..15>        // Setting bass gain (0 = off, 1dB steps)
tonelf       = <0..15>        // Setting bass frequency lower limit x 10 Hz
```

### Get the currently used codec
```const char* currentCodec()```<br>
Returns `STOPPED` if no stream is running.

### Get the filesize
```size_t size()```<br>
Returns `0` if the stream is a radio stream.

### Get the current position in the file
```size_t position()```<br>
Returns `0` if the stream is a radio stream.

### Get the current stream url
```char* lastUrl()```<br>
The current stream url might differ from the request url if the request url points to a playlist.

## Event callbacks
```void audio_showstation(const char* info)```<br>
Returns the station name.

```void audio_showstreamtitle(const char* info)```<br>
Returns ICY stream information.

```void audio_eof_stream(const char* info)```<br>
Is called when the current stream reaches the end of file. Returns the current url.

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


