# Third-party components

## Adafruit GFX Library: classic 5x7 font

`src/font5x7.h` contains the glyph data from `glcdfont.c` in the Adafruit GFX
Library (https://github.com/adafruit/Adafruit-GFX-Library), converted to a
plain C array. The glyph data is unmodified.

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## Raspberry Pi Pico SDK

The firmware is built against and linked with the Raspberry Pi Pico SDK
(https://github.com/raspberrypi/pico-sdk), which is not included in this
repository. `pico_sdk_import.cmake` is copied from the SDK's `external/`
directory. Copyright (c) 2020 Raspberry Pi (Trading) Ltd., BSD 3-Clause
licence (see `LICENSE.TXT` in the SDK).

## TinyUSB

The Pico SDK's USB stdio links TinyUSB (https://github.com/hathach/tinyusb).
Copyright (c) 2018 hathach (tinyusb.org), MIT licence.
