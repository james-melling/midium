# MIDIum (NSK MT-1)

MIDIum is a pocket inline MIDI tester for show-control lines. You plug it
between two devices. It has two MIDI inputs (IN A, IN B) and two outputs
(OUT A, OUT B). It passes MIDI through with one byte time of latency (about
320 µs), can merge the two inputs, shows every incoming message on screen
tagged with its input, and can merge test messages into the outputs without
breaking the stream. When it's unpowered or crashes, two relays wire IN A
straight to OUT A. **IN B is not passed through when the box is unpowered.**

This is the firmware (v0.3, milestone 1 plus the second input) for the
breadboard prototype on a Raspberry Pi Pico 2 (RP2350). For the hookup, see
[docs/wiring.md](docs/wiring.md).

## Features

* **Two inputs, routing matrix.** Each output takes any mix of IN A, IN B
  and the generator. Presets: `A->A, A->B (thru)` (the default: IN A to both
  outputs, IN B monitored only), `A+B merge -> both`, `A->A, B->B (split)`,
  `B->A, B->B`, plus a custom grid.
* **Thru / merge engine on core1.** Byte-level thru: with one source on an
  output, each byte leaves the moment it arrives, byte-identical to the
  input. When two sources share an output, they are interleaved only between
  complete messages, with running status re-sent per output when the source
  changes. Realtime bytes from any input pass straight through, anywhere in
  the stream. Sysex is never interleaved with anything but realtime.
  Generated messages merge the same way. Downstream gear never sees a broken
  message. If the two inputs together send more than one line can carry,
  whole messages are dropped and counted, never parts of messages.
* **Monitor.** One MIDI 1.0 parser per input decodes messages to short text: notes
  with names, CC, PC, pitch bend, aftertouch, sysex with length and
  manufacturer ID, **MSC**, **MMC**, MTC quarter and full frame, song
  position, clock and transport. It keeps a 128-message history and per-type
  counters. Sysex is kept up to 128 bytes per message (the MSC limit). Each
  line starts with its input, `A` or `B`. Display > Show inputs picks A+B,
  A only or B only.
* **MIDI Show Control decoder** (MSC 1.1.1 including two-phase commit). Every
  command format and command in the spec is named. The monitor line reads
  like `MSC LTG GO Q12.5 L1 @1`, `MSC SND STOP`, `MSC LTG SET 3=255` or
  `MSC TGO Q4 01:02:03`. It shortens itself to fit the screen: first the
  device (`@1`, `@G3` for a group, nothing for all-call), then the format,
  then the path and frames. A trailing `~` means fields were left out, and a
  trailing `!` means the message breaks the spec. Pause on an MSC line and
  press to open the **detail page**. It shows the device, the format (full
  name), the command, cue / list / path, time (hh:mm:ss:ff.sf, frame rate,
  colour-frame and status bits), SET control and value (with the lighting
  meaning, e.g. "sub master 4"), the FIRE macro, and the 2PC checksum
  (verified), sequence number, d1-d4 and status code (as text). Any
  warnings follow, then the raw hex. Any other sysex gets the same page
  with its ID, length and hex.
* **Generator.** Note test, note sweep, CC, program change, pitch bend sweep,
  MIDI clock (30-300 BPM, accurate timing), Start / Stop / Continue, MSC (any
  command format and device ID; GO, STOP, RESUME, TIMED_GO, LOAD, SET, FIRE,
  ALL_OFF, RESTORE, RESET, GO_OFF with cue, list, time, control / value or
  macro), MMC Play / Stop, and All Notes Off panic on all 16 channels.
* **Two display options**, detected at boot: a 1.3" SH1106 / 0.96" SSD1306 OLED
  (21 x 8 characters), or a 1.9" ST7789 colour TFT (26 x 10 characters,
  colour-coded by message type).
* **USB debug log** (CDC serial). Each decoded message is printed with its
  timestamp and input, and the log can be turned off in the menu.

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
unterminated), the decoder's text output, the MSC decoder (every format and
command, device IDs, cue / list / path, time code at every frame rate, SET,
FIRE, 2PC checksums and status codes, malformed and fuzzed input, the
detail page, and a generator round trip), routing presets and line widths,
and directed merge cases: the boundary rule, running-status restore per
output, sysex protection, realtime inside sysex, stall timeouts with one and
two inputs, routing latched at message starts, lane overflow (whole-message
drops), and the per-input monitor ring.

Randomised runs push 200-700 kB per input through the engine one byte time
at a time: one input with heavy generator load; the single-input fast path
with IN B streaming unrouted (checked byte-identical and in the same byte
time); split and merged routing; IN A sending 2-10 kB sysex dumps at full
rate while IN B streams; routing presets switched every 3 ms and every
50 ms; and two inputs at 100% (overload). Every run checks that each output
carries every routed message of each input intact and in order, that any
loss is whole messages and matches the drop counters exactly, that every
generated message and every realtime byte arrives (realtime never waits
behind anything but other realtime), and that the TX backlog stays bounded.

## Flashing

1. Hold **BOOTSEL** on the Pico 2 and plug in USB. It mounts as a drive
   called `RP2350`.
2. Drag `build/midium.uf2` onto it. The Pico reboots into MIDIum.

## Controls (5-way joystick)

| Where | UP / DOWN | LEFT | RIGHT | PRESS |
|---|---|---|---|---|
| Monitor (home) | scroll history. Pauses the display and the header shows `PAUSED`. | back to live | time column on/off | open menu. When paused on a sysex / MSC line (highlighted): open its detail page |
| Detail page | scroll | back to the monitor | next page (wraps) | next page (wraps) |
| Menu | move (wraps) | back | select / toggle / edit | select / toggle / edit |
| Editing a value | change value (hold to auto-repeat) | done | done | done |
| Editing the MSC cue | change the part in `< >` | integer part, or done | decimal part, or done | done |

The monitor header shows the routing: `MIDIum  A:a  B:a` means both outputs
take IN A; `A:ab B:ab` is the merge, `A:a  B:b` the split, `-` an output fed
by no input (generator routing is not shown here). A `*` at the right edge
means MIDI arrived on either input in the last 150 ms. On the TFT, a `C`
means the clock generator is running. The onboard LED also blinks on every
byte received.

### Menu

* **Monitor**: back to the monitor.
* **Generate**: Channel (1-16), Note test C4 (vel 100, off after 250 ms),
  Note sweep (C3-C5, 120 ms per note), CC number / CC value / Send CC,
  Program / Send PC, PB sweep, Clock BPM / Clock out, MIDI Start / Stop /
  Continue, **MSC >**, MMC Play / Stop, PANIC (CC 123 on all 16 channels).
  The footer shows what was just sent, decoded.
* **Generate > MSC**: Send, Command, Format (ALL 7F, the general categories
  LTG / SND / MACH / VID / PROJ / PROC / PYRO, then every specific format),
  Device (0-111, G1-G15, ALL), Cue (`integer.decimal`, 0-9999 . 0-99; `none`
  = no cue), Cue list (sent only with a cue), Time (TIMED_GO time, and SET
  fade time when not 0:00; sent as 25 fps), SET control / SET value (0-16383),
  FIRE macro. Defaults are GO, ALL format, all-call, cue 1. LOAD without a
  cue is refused, because the spec requires one.
* **Routing**: the four presets (a `*` marks the active one) and
  **Custom >**, the grid: `OUT A <- IN A`, `OUT A <- IN B`, `OUT A <- Gen`,
  `OUT B <- IN A`, `OUT B <- IN B`, `OUT B <- Gen`. Presets set the input
  columns only and leave generator routing alone. Changes take effect at
  each input's next message, so an output never gets half a message. (This
  replaces the old Thru menu: "Thru A off" is `OUT A <- IN A` off.)
* **Display**: Hide realtime (default on), Note names (C4=60 or C3=60),
  Time column, Brightness, USB log, Show inputs (A+B, A only, B only), Show
  MSC only, MSC dev filter (all, or one device ID; all-call messages always
  pass), Clear monitor.
* **Stats**: total messages, then per input: bytes, messages, UART errors,
  monitor overflow, merge drops (whole messages dropped because that input's
  merge buffer was full), orphan data bytes and stalls. Then counts per
  message type, stray data, TX drops per output (should always be 0), input
  realtime shed, generator counts, stalls, uptime.
* **About**: model, version, build date, which display was detected, the
  last reset cause (power-on or watchdog), and a reminder that unpowered
  only IN A reaches OUT A (IN B is not passed).

Notes on the display:

* Channels are shown 1-16. Program numbers and CC values are shown as the
  raw 0-127 wire values.
* Note names use **C4 = 60** (Roland / scientific) by default. Display >
  Note names switches to **C3 = 60** (Yamaha). That setting will be saved
  once settings persistence lands in M2.
* "Hide realtime" hides clock and active sensing from the monitor history,
  but they are still counted in Stats. It applies as messages arrive, so a
  running clock can't flush the history in a few seconds. "Show inputs",
  "Show MSC only" and "MSC dev filter" work the same way. Errors are always
  shown by the MSC filters; "Show inputs" hides the other input's errors too.
  The USB log is not filtered by these.
* The monitor shows the two inputs, not what the generator sends (the
  footer in the menus shows that). The MSC / sysex detail page shows which
  input the message came from.
* Timestamps (time column, USB log) are `mm:ss.mmm` since boot. Real time of
  day arrives with the RTC in M2.

## Thru / merge engine

core1 does nothing but run this loop from SRAM:

1. Read each byte from UART0 RX (IN A) and UART1 RX (IN B) and give it to
   the engine with its input. Each input has its own parser (running status,
   sysex, system common). Every byte is also copied, with a timestamp and
   its input, into a 4096-entry ring for the core0 monitor.
2. **Routing** latches at the start of each input message: the message goes
   to the outputs that take that input at that moment.
3. **Merging.** Per input, per output there is a 1 kB merge buffer (lane);
   the generator has a 256-byte lane per output. Each output's merger moves
   complete messages from the lanes to the TX queue. While one source is part
   way through a message on an output, the other input's bytes wait in their
   lane. At each message boundary the generator goes first (bounded, see 7),
   then the inputs take turns, one message each. A message that is still
   trickling in (bytes more than ~1 ms apart) waits while the other input has
   a message ready. With one source active the lane is always empty and each
   byte goes straight out: one byte time of latency, byte-identical.
4. **Realtime bytes** (F8-FF), from either input or the generator, may go
   anywhere in the stream (also inside sysex). They bypass the lanes and go
   into a priority queue for each output they are routed to.
5. **Running status.** For each output, the engine remembers the last status
   byte it actually sent. When an input message relies on running status and
   that output's last status differs (because the other input or the
   generator sent something in between), the status byte is re-sent first.
   Explicit status bytes are always passed on. System common and sysex
   clear running status, as the spec requires.
6. **Merge buffer full.** If a message does not fit in its lane (for
   example IN A sends a 10 kB sysex dump while IN B keeps sending to the same
   output), that message is dropped **whole**: the part already buffered is
   taken back and the rest skipped. It is counted in Stats as `IN B merge
   drop`. A message that has started on an output is never cut by this. At
   ~6% load IN B can stream through a full 10 kB dump on IN A without loss;
   heavier IN B traffic during the dump loses some (whole) IN B messages,
   and the dump itself goes through. A new
   input message starts only while that output's TX queue holds 256 bytes
   or less, so when two inputs overload a line the excess waits (and if need
   be is dropped) in the lanes, and TX latency stays under about 120 ms.
7. **Generated messages** arrive from core0 as complete messages through a
   lock-free queue. They go in only at a message boundary on that output, and
   only while the output's TX queue holds 128 bytes or less afterwards, so
   the generator adds at most about 41 ms in front of thru traffic, and only
   on a line that is already close to 100% busy. Generated clock is dropped
   (and counted as "Gen RT shed") rather than delay thru on a saturated line.
8. **Stalls.** If an input stops part-way through a message for more than
   100 ms (1 s in sysex) while another source is waiting for that output,
   the boundary is forced. The rest of that broken input message is then
   dropped on that output, because the downstream receiver has already
   thrown it away. With nobody waiting, a slow input is never cut.
9. **Latency.** A byte is written to the UART only when its hardware FIFO is
   empty, so at most one byte waits in hardware. Single-source thru latency
   is one byte time; realtime waits at most one byte time plus any other
   realtime byte queued at the same instant.
10. **Watchdog.** If core1 ever stops, the chip resets within 250 ms and the
    relays drop out to hard-wired bypass (IN A to OUT A only).
11. **Routing hook.** There is a routing stage per output (`me_route_byte()`)
    where M4 filter and transform rules will plug in. Today it passes
    everything through.

To check that core1 never runs from flash, follow the calls from
`core1_main` in `arm-none-eabi-objdump -d build/midium.elf`: every function
it reaches (`me_input_byte`, `pump`, `me_service`, `me_out_pop`,
`me_mon_push`, and the SDK's 64-bit divide) must sit at a 0x2000xxxx (SRAM)
address, with no literal-pool references to 0x10xxxxxx.

## Code layout

| Path | What |
|---|---|
| `include/pins.h` | the pin map (single source of truth): IN A = GP1 (UART0 RX), OUT A = GP0, IN B = GP5 (UART1 RX), OUT B = GP4, bypass relays = GP2 |
| `include/config.h` | build options, TFT geometry, timings |
| `src/midi_engine.c` | thru / merge engine: two input parsers, per-output mergers (portable, unit tested) |
| `src/midi_io.c` | UART setup, bypass relays, core1 loop (both inputs), watchdog |
| `src/midi_parser.c` | MIDI 1.0 parser (portable, unit tested) |
| `src/midi_decode.c` | message to text and colour class (portable, unit tested) |
| `src/msc_decode.c` | MIDI Show Control decoder, summary / detail text, message builder (portable, unit tested) |
| `src/monitor.c` | core0 input ring to one parser per input, history, counters |
| `src/generator.c` | test message generator and clock |
| `src/console.c` | character-grid console with dirty-row tracking |
| `src/disp_oled.c`, `src/disp_tft.c` | display drivers |
| `src/joystick.c` | 1 ms sampling, debounce, auto-repeat |
| `src/ui.c` | monitor view and menus |
| `src/settings.c` | settings and routing presets (RAM only for now) |
| `src/debug_log.c` | USB CDC log (isolated for M3) |
| `test/` | host unit tests |

## Roadmap

| Milestone | Scope |
|---|---|
| **M1** (this, v0.3) | Thru / merge engine with two inputs and a routing matrix, monitor, generator, menus, OLED / TFT, USB debug log |
| **M2** | RV-3028 RTC (real timestamps), message log as a flash ring buffer plus microSD archive, settings saved to flash, battery fuel gauge (MAX17048) |
| **M3** | USB composite device (class-compliant MIDI plus mass storage for logs), replacing the CDC debug log. XRC host/remote mode with selectable 5 V / 12 V VCC. GPI in and out (3.5 mm TRS) |
| **M4** | Filter / transform rules: channel remap, per-type and per-channel filters, per output |

## Licence

MIT, see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
