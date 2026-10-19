# MIDIum (NSK MT-1)

MIDIum is a pocket inline MIDI tester for show-control lines. You plug it
between two devices. It has two MIDI inputs (IN A, IN B) and two outputs
(OUT A, OUT B). It passes MIDI through with one byte time of latency (about
320 µs), can merge the two inputs, shows every incoming message on screen
tagged with its input, and can merge test messages into the outputs without
breaking the stream. When it's unpowered or crashes, two relays wire IN A
straight to OUT A. **IN B is not passed through when the box is unpowered.**

This is the firmware (v0.5: milestone 1, the second input, and milestone 2
so far: real-time clock, MTC reader, flash logging, soft power on / off and
the battery fuel gauge) for the breadboard prototype on a Raspberry Pi Pico 2
(RP2350). For the hookup, see
[docs/wiring.md](docs/wiring.md). A browser simulator runs the same code
([sim/](sim/), page `sim/web/index.html`).

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
  line starts with its input, `A` or `B`. The **newest message is pinned to
  the bottom row**, highlighted, even while you scroll back through the
  history above it (Display > Pin latest turns this off).
* **Filters** (Display > Filters): show or hide each message type (notes,
  poly AT, CC, program change, channel AT, pitch bend, other sysex, MSC,
  MMC, MTC quarter frames, MTC full frames, song position / select, tune
  request, clock, start / stop / continue, active sensing, system reset,
  undefined, errors), with presets *All*, *Show control only* (MSC, MMC,
  MTC full frame, start / stop / continue, program change), *No
  realtime/TC* (the default: everything but clock, active sensing and MTC
  quarter frames) and *None*. Also a MIDI channel filter, the input filter
  (A+B, A only, B only) and the MSC device filter.
* **MTC reader.** MIDI Time Code from either input is rebuilt from quarter
  frames (forward and reverse, 24 / 25 / 29.97 drop-frame / 30 fps) and
  full-frame locates. While it runs, the monitor header shows the time
  (`01:00:03:12 25fps A`) instead of the routing, and *MTC display* shows it
  full screen in large digits.
* **Real-time clock** (RV-3028-C7 on the I2C bus) for time-of-day
  timestamps to the millisecond, set from Settings > Date & time. Without
  the RTC a software clock can be set by hand (lost at power off).
* **Logging.** Every message received (all of them, whatever the display
  filters say) is logged to the Pico's flash with its input and timestamp,
  in sessions: a new session at every power-up and on demand. Menu > Logs
  lists them and opens any one in a monitor-style browser, with the same
  filters and detail pages.
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
* **Soft power and battery** (v0.5). The joystick press is also the power
  button: hold it to power off, with a countdown you can cancel. Power-off
  always closes the log cleanly and drops the relays to passive thru. With
  a MAX17048 fuel gauge fitted: battery icon and % in the header, charging
  shown, low-battery warning, popup and safe automatic shutdown, and
  optional auto-off on battery. See [Power](#power).
* **USB debug log** (CDC serial). Each decoded message is printed with its
  timestamp and input, and the log can be turned off in the menu. The type
  filters apply to it (input, channel and device filters do not).

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

The flash log region (`LOG_FLASH_OFFSET`, `LOG_FLASH_SIZE`, default the top
3 MB of the Pico 2's 4 MB flash) is in `include/config.h`.

TFT geometry (`TFT_X_OFFSET`, `TFT_Y_OFFSET`, `TFT_MADCTL`) is in
`include/config.h`.

### Host unit tests

The parser, decoders, thru/merge engine, MTC reader, filters, value
editing, calendar code and flash log format are plain C. They have a PC test
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

v0.5 adds: the power-button state machine (taps of every length act once,
on release; a hold never also selects; cancel; power off; a press held at
power-up is ignored; timer wrap-around), auto-off timing for every period
(warning at 60 s, any activity cancels, never on USB, unplugging starts a
full period), the low-battery thresholds (warning 10 %, one popup at 5 %
re-armed by charging, shutdown at 3 % or below 3.3 V after three readings,
never on USB or without a gauge), the MAX17048 register conversions, and
the log's session close record (clean, auto-off and low-battery closes read
back after a reboot, power loss and a torn close page detected, empty
sessions, 120 closed sessions through the ring).

v0.4 adds: calendar conversion for every day 2000-2099; MTC frame counting
(drop-frame: 107,892 frames per hour), quarter-frame reassembly at all four
rates in both directions over 600 sets each (checked against a transmitter
model, including the +2 / -2 frame offset and per-frame interpolation),
partial sets, gaps, direction changes and full frames; every filter category,
preset, channel, input and MSC device rule; value wrap-around and hold
acceleration; the seven-segment glyphs; and the flash log: sessions written
and read back forwards and backwards (with out-of-order timestamps and real
time), reboot (index rebuilt from flash), empty sessions, a clock change
mid-page, ring wrap-around (7x the region, oldest session partial), delete
all, 24 power cuts mid-page (torn pages skipped, writing carries on), CRC
errors in headers and payloads (at boot and while reading), garbage slots
skipped, and 100,000 messages with the flash servicing only every third
message.

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

PRESS acts when you **let go** of a short press. Holding PRESS for 2 s
anywhere starts the power-off countdown instead (see [Power](#power)), so
a press that turns into a hold never also selects something.

| Where | UP / DOWN | LEFT | RIGHT | PRESS |
|---|---|---|---|---|
| Monitor (home) | scroll history. Pauses the display and the header shows `PAUSED`. The first UP selects the pinned newest line; the next ones move the cursor (`A>`) up the history. | back to live | time column: off / uptime / time of day | open menu. When paused on a sysex / MSC line (cursor, or the pinned line selected): open its detail page |
| Detail page | scroll | back (monitor or log browser) | next page (wraps) | next page (wraps) |
| Menu | move (wraps) | back | select / toggle / edit | select / toggle / edit |
| Editing a value | change value. **Wraps at both ends** (127 -> 0, 0 -> 127). Hold to auto-repeat; on ranges of more than 150 steps, steps of 10 after ~1.3 s and 100 after ~3.2 s (a big step stops on the end value before wrapping) | done | done | done |
| Editing the MSC cue | change the part in `< >` (wraps) | integer part, or done | decimal part, or done | done |
| MTC display | - | back | - | back |
| Date & time | (editing) change the field in `< >`, wraps within its range (month 1-12, day 1 to the month's length, hour 0-23, ...) | previous field; on the year: cancel | next field | edit; while editing: set the clock |
| Logs | move (wraps) | back | open | open the session / start a new session / delete all |
| Log browser | scroll (older / newer) | back to Logs | time column | detail page of an MSC / sysex line |
| Power off? | choose Cancel / Power off | cancel | ok | ok |
| Anywhere | | | | hold 2 s: power-off countdown; release to cancel, hold 2 s more to power off |

The monitor header shows the routing: `MIDIum  A:a  B:a` means both outputs
take IN A; `A:ab B:ab` is the merge, `A:a  B:b` the split, `-` an output fed
by no input (generator routing is not shown here). A `*` at the right edge
means MIDI arrived on either input in the last 150 ms. On the TFT, a `C`
means the clock generator is running. The onboard LED also blinks on every
byte received. With a fuel gauge fitted, a battery icon and the charge sit
just left of these (`MIDIum A:a  B:a ▮ 76` on the OLED, which drops one
space after the name to make room; `▮ 76%` on the TFT); see [Power](#power).

While MTC is running (a quarter frame in the last 250 ms) the header shows
the time instead, with the rate and the input: `01:00:03;12 29.97df A` on
the TFT (`rev` when running backwards), `01:00:03;12 29D A` on the OLED
(`24`, `25`, `29D`, `30`, `R` for reverse). A `;` before the frames means
drop-frame. When MTC stops, the last time stays for 2 s marked `STOP` (dimmed
on the TFT), then the routing summary comes back. A full-frame locate shows
the same way.

In the pinned monitor the bottom row is always the newest message (inverse).
Scrolling freezes the rows above it; new messages keep replacing the pinned
line, and the history between the frozen rows and the newest message is
reached by scrolling down. The cursor line is marked `A>` / `B>` (and a grey
bar on the TFT, an underline on the OLED).

### Menu

* **Monitor**: back to the monitor.
* **MTC display**: the current MTC time full screen in seven-segment digits
  (both displays), updated every frame, with RUNNING FWD / REV, STOPPED,
  LOCATED (full frame only) or NO MTC, the rate, drop-frame or not, and the
  input. Amber while running, grey when stopped. LEFT goes back.
* **Logs**: `+ New log session`, then the sessions newest first, each with
  its number, start date and time (or `up h:mm:ss` after power-up when the
  clock was not set; `~` = its oldest part has been overwritten), duration
  and message count, the current one marked LIVE; then `x Delete all logs`,
  which asks again (Cancel is preselected). Press a session to browse it:
  the same lines as the monitor, newest at the bottom (highlighted),
  UP / DOWN to scroll, the current filters applied, PRESS on MSC / sysex for
  the detail page.
* **Generate**: Channel (1-16), Note test C4 (vel 100, off after 250 ms),
  Note sweep (C3-C5, 120 ms per note), CC number / CC value / Send CC,
  Program / Send PC, PB sweep, Clock BPM / Clock out, MIDI Start / Stop /
  Continue, **MSC >**, MMC Play / Stop, PANIC (CC 123 on all 16 channels).
  The footer shows what was just sent, decoded.
* **Generate > MSC**: Send, Command, Format (ALL 7F, the general categories
  LTG / SND / MACH / VID / PROJ / PROC / PYRO, then every specific format),
  Device (0-111, G1-G15, ALL; every value wraps), Cue (`integer.decimal`, 0-9999 . 0-99; `none`
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
* **Display**: Pin latest (default on), **Filters >**, Note names (C4=60 or
  C3=60), Time column (Off, Uptime `mm:ss.mmm`, Time of day
  `hh:mm:ss.mmm`; `hh:mm:ss` on the OLED), Brightness, USB log, Clear
  monitor.
* **Display > Filters**: the presets (a `*` marks the one that matches),
  then one on/off line per message type, then Channel (all, 1-16: hides
  channel messages on other channels; system messages always pass), Show
  inputs (A+B, A only, B only) and MSC dev filter (all, or one device ID;
  all-call messages always pass). Presets leave the Errors line alone.
* **Settings > USB power-on**: *Charge only* (default) or *Power on*: what
  plugging USB into a box that is off does. Needs the power controller and
  its VBUS strap; see [Power](#power).
* **Settings > Auto-off (batt)**: Off (default), 15, 30, 60 or 120 min with
  no MIDI in and no key presses, on battery only.
* **Power off**: asks again (Cancel is preselected), then shuts down
  cleanly, the same as holding the button.
* **Settings > Date & time**: whether the RTC was found, whether the time is
  valid (`not set`, `lost - set it` after the RTC lost power), the backup
  mode, and the current date and time. Press to set it (starts from the
  current time, or the firmware build date).
* **Stats**: battery charge (to 0.1 %), cell voltage, power source (USB,
  charging / USB, full / battery), charge rate (%/h), time to auto-off,
  uptime and how the last power-down went (*clean*, *low battery*,
  *auto-off*, *power loss*, *watchdog*). Then total messages, then per input: bytes, messages, UART errors,
  monitor overflow, merge drops (whole messages dropped because that input's
  merge buffer was full), orphan data bytes and stalls. Then counts per
  message type, stray data, TX drops per output (should always be 0), input
  realtime shed, generator counts, stalls. Log: % of the log flash
  used, sessions, the live session's number and message count, records
  dropped (RAM queue full), bad pages found, pages written, sector erases.
* **About**: model, version, build date, which display was detected, the
  RTC, last reset cause (power-on or watchdog), last power-down, the gauge
  (MAX17048 or none), and a reminder that unpowered
  only IN A reaches OUT A (IN B is not passed).

Notes on the display:

* Channels are shown 1-16. Program numbers and CC values are shown as the
  raw 0-127 wire values.
* Note names use **C4 = 60** (Roland / scientific) by default. Display >
  Note names switches to **C3 = 60** (Yamaha). That setting will be saved
  once settings persistence lands in M2.
* Filters apply to the monitor as messages arrive (so a running clock can't
  flush the history in a few seconds); messages already in the history stay.
  Hidden messages are still counted in Stats and still logged to flash. In
  the log browser the filters apply when you look, so changing them shows
  a session differently. "Show inputs" hides the other input's errors too.
* The monitor shows the two inputs, not what the generator sends (the
  footer in the menus shows that). The MSC / sysex detail page shows which
  input the message came from.
* Timestamps: the time column shows uptime (`mm:ss.mmm` since boot) or the
  time of day (from the RTC, or the clock set by hand; uptime is shown in its
  place until the time is set). The log stores the time of day when it is
  valid, else uptime. The USB log prints uptime.

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

## Power

### Hardware

On the final PCB a push-button on/off controller (LTC2955 or MAX16150
class) switches the system supply. The joystick centre switch is wired to
both GP22 (as now) and the controller's PB input through diodes, so pressing
it while the box is off powers it on. The firmware drives **GP3
(PWR_KILL)** to its inactive level as the very first thing at boot to keep
the power on, and to its active level to power off. The controller also
forces power off by itself after a long hold (about 8 s), whatever the
firmware is doing. GP3 is active-low by default (LTC2955 KILL, MAX16150
CLR, or a P-FET hold circuit read as "low = off"); `PWR_KILL_ACTIVE_LOW` in
`include/config.h` flips it. On the breadboard prototype nothing is
connected to GP3. Build with `-DMIDIUM_PWR_CTRL=1` on the final PCB (see
USB power-on below).

USB power is sensed on **GP24**: the Pico 2 board feeds VBUS to it
through a divider (the SDK's `PICO_VBUS_PIN`). `PIN_VBUS_SENSE` in
`include/pins.h` moves it, or `-1` turns it off (the box then counts as on
battery whenever a gauge answers). The charger's status pin (`PIN_CHG_STAT`)
is reserved for the PCB (GP23 there) and unused on the prototype, where
*charging* means USB present and the gauge below 100 %.

The fuel gauge is a **MAX17048** on I2C1 with the OLED and the RTC
(address **0x36**). Without it there is no battery UI, no low-battery
handling and no auto-off. Its ALRT pin is not used (the firmware polls once
a second). See [docs/wiring.md](docs/wiring.md).

### Powering off

* **Hold PRESS** on any screen. After 2 s the screen shows *Power off...
  release to cancel* with a bar that fills over 2 s more. Let go before the
  end and nothing happens (no menu opens, nothing is selected). Hold to the
  end to power off. (`PWR_HOLD_MS`, `PWR_COUNTDOWN_MS` in `config.h`.)
* **Menu > Power off**, then *Power off* (Cancel is preselected).
* **Low battery** and **auto-off** (below) use the same sequence.

The sequence: *Shutting down*: the generator stops (clock, sweeps), the
log session is closed and everything in RAM is written to flash (with a
close record, so it is not counted as a power loss), core1 is stopped
cleanly after its output queues drain, and the bypass relays drop: IN A is
wired to OUT A again (passive thru), IN B goes nowhere, exactly as when the
box is unpowered. Then *Off / Safe to unplug*, and GP3 cuts the power.

If the power does not go within 1 s (the breadboard, which has no
controller, or anything else keeping the supply up), the box goes to
**soft-off**: *Off, press the joystick to power on*, then the display
turns off after 3 s. The relays stay released and core1 stays stopped. A
press reboots the box (a watchdog reset, marked so it is not reported as a
crash).

### Powering on

Press the joystick. The press that powered the box on is ignored by the
firmware, however long it is held, so it cannot open the menu or start
the power-off countdown.

**Settings > USB power-on** decides what plugging USB into a box that is
off does. The controller has to be told in hardware: route the USB VBUS
detect to the controller's ON input (the LTC2955 has one; for a part
without, OR it into PB; check the datasheet of the part chosen) through a
jumper or an option resistor. Without that strap USB only
charges. With it fitted the box powers up on USB, and a firmware built with
`MIDIUM_PWR_CTRL=1` then checks the setting: with *Charge only* (default) a
power-up caused by USB (VBUS present, the button not pressed at power-up)
goes straight to a *Charging* screen (no logging, relays released, display
off after 10 s; a press powers on properly; unplugging USB powers off). With
*Power on* it starts normally. Settings are not saved to flash yet (M2), so
until they are the default applies at every power-up.

### Battery

Read once a second from the gauge. The monitor header shows an icon (four
fill levels, or a lightning bolt while charging) and the charge: `▮ 76` on
the OLED, `▮ 76%` on the TFT. While charging the number alternates with
`CHG`. During MTC the icon and number appear if the time leaves room, else
just the icon.

* **10 %** on battery: the field alternates with `LOW`, out of the header's
  inverse (red on the TFT).
* **5 %**: a *Battery low* popup, once (again after charging, or after the
  charge climbs back 2 % above it). Any key closes it.
* **3 %**, or the cell below **3.3 V**, for three readings in a row: clean
  shutdown (*Battery empty*), so the log is kept.
* Never while USB is connected. Thresholds: `BATT_*` in `config.h`.

### Auto-off

**Settings > Auto-off (batt)**: after the set time with no MIDI arriving on
either input, no key presses and the generator idle (clock, sweeps), on
battery only, the box powers off cleanly. 60 s before, *Auto power off*
counts down; any key cancels it (and does nothing else), as does MIDI.
On USB the timer does not run, and unplugging starts a full period. Stats
shows the time left.

### Last power-down

At power-up the firmware looks at the newest log session: a close record
gives *clean*, *low battery* or *auto-off*; no close record means *power
loss* (or *watchdog* if the chip was reset by the core1 watchdog). Every
power-up now writes its session's start page at once, so a power-up with
no MIDI still counts. Shown in Stats and About.

## Real-time clock

The RV-3028-C7 sits on I2C1 with the OLED (address 0x52). At boot the
firmware checks its PORF flag (power was lost: time invalid), sets 24-hour
mode, and sets the backup switchover to direct switching mode (written to
the RTC's EEPROM once; trickle charging is never enabled). It then watches
the seconds register until it ticks and latches the microsecond timer at
that moment (up to 1.1 s, during the splash), so a timestamp is the RTC
second plus the timer since the latch, to within a few ms. It re-latches
every 10 minutes. Setting the time writes the RTC (which restarts its
second) and clears PORF. Without an RTC the same mapping runs from a time
set by hand. Messages are stamped by core1 with the timer when their first
byte arrives; the time of day is derived from that, so it is as exact as
the uptime stamp.

## Flash log

The log lives in the Pico 2's QSPI flash, in the region set by
`LOG_FLASH_OFFSET` / `LOG_FLASH_SIZE` in `include/config.h` (default: the
top 3 MB; the firmware is about 140 kB, and logging is disabled at boot if
the image ever reaches the region). The format code (`src/log_store.c`) is
portable: the same code runs on the RP2350 (`src/log_flash_rp2350.c`), in
the simulator and the host tests (`src/log_flash_ram.c`, a RAM emulation of
NOR flash), and can later sit on a microSD archive.

* **Layout.** 4 kB sectors (the erase unit) of 16 pages of 256 bytes. Each
  page has a 32-byte header (magic, kind, record count, page sequence
  number, session number, length, base uptime and base time of day, CRC-32)
  and 224 bytes of records. The first page of a session also holds the
  session's start time. A record is: input and flags, the time since the
  previous record (variable length, microseconds), and the raw message
  bytes with their status byte (sysex: the first 128 bytes plus the total
  length, as the monitor keeps). A note on takes about 7 bytes, a clock or
  a quarter frame 5-6, so a page holds 30-45 short messages and 3 MB about
  450,000 of them (about 1.25 hours of continuous 25 fps MTC on one input;
  weeks of cues and notes).
* **Closing.** A clean shutdown writes a close page (kind 3: end time and
  reason) after the session's last page. A session without one ended in a
  power loss. v0.4 firmware skips close pages (it counts them as bad pages).
* **Writing.** Records collect in a RAM page; full pages queue in RAM
  (16 pages) and are programmed from the core0 main loop (~1 ms each). A
  part-filled page is written after 2 s, so a power cut loses at most the
  last 2 s. The next sector is erased ahead of time, when the inputs have
  been quiet for 100 ms and the clock generator is off, or anyway once 8
  pages are waiting. Erasing ahead drops the oldest sector: the ring
  overwrites the oldest session first, which then shows `~`.
* **Reading back.** At boot every page is checked (magic and CRC). The
  newest valid page sets the write position; a page cut short by a power
  cut or with a bad CRC is skipped (and counted in Stats), and the writer
  steps over any slot that is not blank. Sessions are rebuilt from the
  pages (the newest 128 are listed).
* **Why flash writes don't touch thru.** While the flash is being written
  it cannot be read, so nothing may execute from it. The SDK's usual
  answer pauses the other core; MIDIum does not, because core1 runs the
  thru path. Instead core1 never uses flash at all: its code and data are
  in SRAM, it has no interrupts (it polls), and it uses no const tables.
  core0 disables only its own interrupts for each erase / program and calls
  the SDK routines, which run from RAM. The UI pauses for that time (~1 ms
  per page, ~45 ms per sector erase); the 4096-entry monitor ring holds the
  input meanwhile. Details in `src/log_flash_rp2350.c`.

## Code layout

| Path | What |
|---|---|
| `include/pins.h` | the pin map (single source of truth): IN A = GP1 (UART0 RX), OUT A = GP0, IN B = GP5 (UART1 RX), OUT B = GP4, bypass relays = GP2, PWR_KILL = GP3, I2C1 (OLED, RTC 0x52, gauge 0x36) = GP6 / GP7, VBUS sense = GP24 |
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
| `src/ui.c` | monitor view, menus, MTC display, date & time, logs and log browser |
| `src/settings.c` | settings, filter defaults and routing presets (RAM only for now) |
| `src/debug_log.c` | USB CDC log (isolated for M3) |
| `src/mtc.c` | MTC reader: quarter frames, full frames, drop-frame counting (portable, unit tested) |
| `src/msg_filter.c` | monitor / log browser filters (portable, unit tested) |
| `src/valedit.c` | wrap-around value stepping and hold acceleration for every editor (portable, unit tested) |
| `src/bigdigits.c` | seven-segment digits for the MTC display (portable, unit tested) |
| `src/timeutil.c` | calendar conversions and formatting (portable, unit tested) |
| `src/rv3028.c`, `src/clock.c` | RTC driver; time of day (RTC second + microsecond timer, or software clock) |
| `src/log_store.c` | flash log format: pages, sessions, ring, reader (portable, unit tested) |
| `src/log_flash_rp2350.c`, `src/log_flash_ram.c` | log backends: QSPI flash, RAM NOR emulation |
| `src/logger.c` | logging sessions on core0: stamps, flush and erase pacing, clean close |
| `src/power.c` | soft power (kill line, shutdown sequence, soft-off, charge-only), battery polling, low battery, auto-off |
| `src/power_fsm.c` | power-button state machine, auto-off timer, low-battery thresholds (portable, unit tested) |
| `src/max17048.c` | MAX17048 fuel gauge driver; register conversions in `include/max17048.h` (unit tested) |
| `src/glyphs.h` | battery and charging icons drawn in place of a few control characters |
| `sim/` | browser simulator: the core0 code + engine in WebAssembly, `sim/build.sh` |
| `test/` | host unit tests |

## Roadmap

| Milestone | Scope |
|---|---|
| **M1** (v0.3) | Thru / merge engine with two inputs and a routing matrix, monitor, generator, menus, OLED / TFT, USB debug log |
| **M2** (v0.4, v0.5) | Done: RV-3028 RTC (real timestamps), message log as a flash ring buffer with sessions, MTC reader and display, monitor filters, soft power on / off, MAX17048 fuel gauge, low battery, auto-off. Still to do: microSD archive, settings saved to flash |
| **M3** | USB composite device (class-compliant MIDI plus mass storage for logs), replacing the CDC debug log. XRC host/remote mode with selectable 5 V / 12 V VCC. GPI in and out (3.5 mm TRS) |
| **M4** | Filter / transform rules on the MIDI outputs: channel remap, per-type and per-channel filters, per output (the v0.4 filters are display only) |

## Licence

MIT, see [LICENSE](LICENSE). Third-party components are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).
