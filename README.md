# MIDIum (NSK MT-1)

MIDIum is a pocket inline MIDI tester for show-control lines. You plug it
between two devices. It passes MIDI IN through to OUT A and OUT B with one
byte time of latency (about 320 µs), shows every incoming message on screen,
and can merge test messages into the outputs without breaking the stream.
When it's unpowered or crashes, two relays wire IN straight to OUT A.

This is the **milestone 1** firmware for the breadboard prototype on a
Raspberry Pi Pico 2 (RP2350). For the hookup, see [docs/wiring.md](docs/wiring.md).

## Features (M1)

* **Thru / merge engine on core1.** Byte-level thru from IN to OUT A and OUT B,
  each output switchable. Realtime bytes pass straight through. Running status
  is preserved. Generated messages are merged only at input message
  boundaries, and the running status byte is re-sent when needed, so
  downstream gear never sees a broken message. Sysex passes intact.
* **Monitor.** A full MIDI 1.0 parser decodes messages to short text: notes
  with names, CC, PC, pitch bend, aftertouch, sysex with length and
  manufacturer ID, **MSC** (GO / STOP / RESUME / ... with cue number and
  device ID), **MMC**, MTC quarter and full frame, song position, clock and
  transport. It keeps a 128-message history and per-type counters.
* **Generator.** Note test, note sweep, CC, program change, pitch bend sweep,
  MIDI clock (30-300 BPM, accurate timing), Start / Stop / Continue, MSC GO /
  STOP / RESUME, MMC Play / Stop, and All Notes Off panic on all 16 channels.
* **Two display options**, detected at boot: a 1.3" SH1106 / 0.96" SSD1306 OLED
  (21 x 8 characters), or a 1.9" ST7789 colour TFT (26 x 10 characters,
  colour-coded by message type).
* **USB debug log** (CDC serial). Each decoded message is printed with its
  timestamp, and the log can be turned off in the menu.

## Building

You need the Arm GCC toolchain (`arm-none-eabi-gcc`), CMake 3.13 or later,
and Pico SDK 2.1.1 or later with the TinyUSB submodule.

```sh
git clone --depth 1 --branch 2.1.1 https://github.com/raspberrypi/pico-sdk ~/pico-sdk
git -C ~/pico-sdk submodule update --init --depth 1 lib/tinyusb
export PICO_SDK_PATH=~/pico-sdk

cd midium
mkdir build && cd build
cmake -DPICO_BOARD=pico2 ..
make -j
# -> build/midium.uf2
```

Build options (pass to `cmake` with `-D`):

| Option | Default | Meaning |
|---|---|---|
| `MIDIUM_DISPLAY` | `0` | `0` = auto-detect (OLED if 0x3C answers, else TFT), `1` = OLED, `2` = TFT |
| `MIDIUM_OLED_SSD1306` | `ON` | `OFF` for an SH1106 OLED (default is SSD1306, e.g. Adafruit 938) |

TFT geometry (`TFT_X_OFFSET`, `TFT_Y_OFFSET`, `TFT_MADCTL`) is in
`include/config.h`.

### Host unit tests

The parser, decoder and thru/merge engine are plain C. They have a PC test
suite built with AddressSanitizer and UBSan:

```sh
make -C test
```

The suite covers running status, interleaved realtime, sysex (long, truncated,
unterminated), the decoder's text output, and directed merge cases: the
boundary rule, running-status restore, sysex protection, stall timeout and
thru latching. It also runs three randomised merge runs of 200-400 kB of
dense traffic with heavy generator load. These check that both outputs carry
every input message intact and in order, every generated message intact,
every clock byte, no drops, and bounded latency.

## Flashing

1. Hold **BOOTSEL** on the Pico 2 and plug in USB. It mounts as a drive
   called `RP2350`.
2. Drag `build/midium.uf2` onto it. The Pico reboots into MIDIum.

## Controls (5-way joystick)

| Where | UP / DOWN | LEFT | RIGHT | PRESS |
|---|---|---|---|---|
| Monitor (home) | scroll history. Pauses the display and the header shows `PAUSED`. | back to live | time column on/off | open menu |
| Menu | move (wraps) | back | select / toggle / edit | select / toggle / edit |
| Editing a value | change value (hold to auto-repeat) | done | done | done |

The monitor header shows `MIDIum  A:on B:on` (thru state per output). A `*`
at the right edge means MIDI arrived in the last 150 ms. On the TFT, a `C`
means the clock generator is running. The onboard LED also blinks on every
byte received.

### Menu

* **Monitor**: back to the monitor.
* **Generate**: Channel (1-16), Note test C4 (vel 100, off after 250 ms),
  Note sweep (C3-C5, 120 ms per note), CC number / CC value / Send CC,
  Program / Send PC, PB sweep, Clock BPM / Clock out, MIDI Start / Stop /
  Continue, MSC GO / STOP / RESUME (device 7F all-call, command format 7F,
  no cue number), MMC Play / Stop, PANIC (CC 123 on all 16 channels).
  The footer shows what was just sent.
* **Thru**: Thru A, Thru B, Generator to A, Generator to B.
* **Display**: Hide realtime (default on), Note names (C4=60 or C3=60),
  Time column, Brightness, USB log, Clear monitor.
* **Stats**: counts per message type, bytes in, UART errors, monitor
  overflow, stray data, thru drops, generator counts, stalls, uptime.
* **About**: model, version, build date, which display was detected, and
  the last reset cause (power-on or watchdog).

Notes on the display:

* Channels are shown 1-16. Program numbers and CC values are shown as the
  raw 0-127 wire values.
* Note names use **C4 = 60** (Roland / scientific) by default. Display >
  Note names switches to **C3 = 60** (Yamaha). That setting will be saved
  once settings persistence lands in M2.
* "Hide realtime" hides clock and active sensing from the monitor history,
  but they are still counted in Stats. It applies as messages arrive, so a
  running clock can't flush the history in a few seconds.
* Timestamps (time column, USB log) are `mm:ss.mmm` since boot. Real time of
  day arrives with the RTC in M2.

## Thru / merge engine

core1 does nothing but run this loop from SRAM:

1. Read each byte from UART0 RX and give it to the engine. The engine
   forwards it immediately to each output that has thru enabled, and copies
   it with a timestamp into a 4096-entry ring for the core0 monitor.
2. **Realtime bytes** (F8-FF), input or generated, may go anywhere in the
   stream. They are queued ahead of everything else for that output.
3. **Running status.** For each output, the engine remembers the last status
   byte it actually sent. If an input data byte starts a new running-status
   message and that output's last status differs (because a generated message
   went out in between), the engine re-sends the status byte first. System
   common and sysex clear running status, as the spec requires.
4. **Generated messages** arrive from core0 as complete messages through a
   lock-free queue. They are inserted only at an input message boundary:
   never mid-message, never mid-sysex. If the input stops part-way through a
   message for more than 100 ms (1 s for sysex), the boundary is forced. The
   rest of that broken input message is then dropped on the outputs that got
   the generated message, because the downstream receiver has already thrown
   it away.
5. **Thru on/off** takes effect at the next message start, so an output never
   receives half a message.
6. **Latency.** A byte is written to the UART only when its hardware FIFO is
   empty, so at most one byte waits in hardware. Thru latency is one byte
   time. The generator may add at most 128 bytes (about 41 ms) of backlog in
   front of thru traffic, and only when the input line is already close to
   100% busy. Generated clock is dropped (and counted as "Gen RT shed") rather
   than delay thru on a saturated line.
7. **Watchdog.** If core1 ever stops, the chip resets within 250 ms and the
   relays drop out to hard-wired bypass.
8. **Routing hook.** There is a routing stage per output (`me_route_byte()`)
   where M4 filter and transform rules will plug in. In M1 it passes
   everything through.

## Code layout

| Path | What |
|---|---|
| `include/pins.h` | the pin map (single source of truth) |
| `include/config.h` | build options, TFT geometry, timings |
| `src/midi_engine.c` | thru / merge engine (portable, unit tested) |
| `src/midi_io.c` | UART setup, bypass relays, core1 loop, watchdog |
| `src/midi_parser.c` | MIDI 1.0 parser (portable, unit tested) |
| `src/midi_decode.c` | message to text and colour class (portable, unit tested) |
| `src/monitor.c` | core0 input ring to parser, history, counters |
| `src/generator.c` | test message generator and clock |
| `src/console.c` | character-grid console with dirty-row tracking |
| `src/disp_oled.c`, `src/disp_tft.c` | display drivers |
| `src/joystick.c` | 1 ms sampling, debounce, auto-repeat |
| `src/ui.c` | monitor view and menus |
| `src/settings.c` | settings (RAM only in M1) |
| `src/debug_log.c` | USB CDC log (isolated for M3) |
| `test/` | host unit tests |

## Roadmap

| Milestone | Scope |
|---|---|
| **M1** (this) | Thru / merge engine, monitor, generator, menus, OLED / TFT, USB debug log |
| **M2** | RV-3028 RTC (real timestamps), message log as a flash ring buffer plus microSD archive, settings saved to flash, battery fuel gauge (MAX17048) |
| **M3** | USB composite device (class-compliant MIDI plus mass storage for logs), replacing the CDC debug log. XRC host/remote mode with selectable 5 V / 12 V VCC. GPI in and out (3.5 mm TRS) |
| **M4** | Filter / transform rules: channel remap, per-type and per-channel filters, per output |

## Licence

MIT, see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
