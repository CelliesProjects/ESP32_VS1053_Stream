# ESP32_VS1053_Stream

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/7571166c872e4dc8a899382389b73f8e)](https://app.codacy.com/gh/CelliesProjects/ESP32_VS1053_Stream?utm_source=github.com&utm_medium=referral&utm_content=CelliesProjects/ESP32_VS1053_Stream&utm_campaign=Badge_Grade_Settings)

A streaming library for esp32 with a separate vs1053 mp3/ogg/aac/wav decoder.

With this library you can play http, https (insecure mode) and chunked audio streams.

Supports playback of mp3, ogg, aac, aac+ files/streams.

This library depends on the [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) to communicate with the decoder.

Check out [eStreamPlayer32_VS1053](https://github.com/CelliesProjects/eStreamPlayer32_VS1053) to see a project using this library.

## How to install

Install [ESP_VS1053_Library](https://github.com/baldram/ESP_VS1053_Library) and `ESP32_VS1053_Stream` in your Arduino library folder.

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

-  `stream.startDecoder(CS, DCS, DREQ)`

Will return `true` or `false` depending on the result.

### Starting or resuming a stream

-  `stream.connecttohost(url)`

Will return `true` or `false` depending on the result.

You can resume (or start playing with an offset) by requesting a stream with

-  `stream.connecttohost(url, startrange)`

For streams that need login credentials use

-  `stream.connecttohost(url, user, pwd)`

### Stopping or pausing a stream

Stop a stream with:

-  `stream.stopSong()` 

Pause a stream with:

-  `stream.stopSong(VS1053_RESUME)`

#### Example how to pause a stream

```c++
const String HOST = "http://some_file_with_a_filesize";

stream.connecttohost(HOST);

//[stream starts playing from the start]

const size_t FILE_POS = stream.position();
stream.stopSong(VS1053_RESUME);

//[stream is paused]

stream.connecttohost(HOST, FILE_POS);

//[stream will resume from FILE_POS]

```
This is only needed on real files. Radiostreams always return `0` on `stream.position()` and `stream.size()`.

If a stream reaches EOF `stream.stopSong()` is called automagically. 
<br>In that case you can use `stream.connecttohost(NEW_HOST)` without first calling `stream.stopSong()`.

### Feeding the decoder

-  `stream.loop()`

This function has to called every couple of ms to feed the decoder with data.

### Check if stream is running

-  `stream.isRunning()`

Will return `true` or `false`.

### Get the current volume

-  `stream.getVolume()`

### Set the volume

-  `stream.setVolume(value)`

Value should be between 0-100.

### Get the currently used codec
-  `stream.currentCodec()`

Return a string with the currently used codec.
Returns `UNKNOWN` if no stream is running.

### Get the filesize
-  `stream.size()`

Will return `0` if the stream is a radio stream.

### Get the current position in the stream
-  `stream.position()`

Will return `0` if the stream is a radio stream.

### Get the current url
-  `stream.lastUrl()`

The current stream url might differ from the request url if the request url points to a playlist.

## Event callbacks

-  `audio_showstation(const char* info)`

Returns the station name.

-  `audio_showstreamtitle(const char* info)`

Returns ICY stream information.

-  `audio_eof_stream(const char* info)`

Is called when the current stream reaches the end of file.

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


