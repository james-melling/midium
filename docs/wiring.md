# MIDIum (NSK MT-1) breadboard wiring, milestone 1

Prototype on a **Raspberry Pi Pico 2** (RP2350). Everything runs at 3.3 V except
the relay coils (5 V from VBUS). The pin map matches `include/pins.h`. If you
change one, change the other.

"Pico pin" is the physical pin number on the Pico 2 header (pin 1 = GP0, top left
with USB at the top).

## 1. Pin map

| GPIO | Pico pin | Function | Connects to |
|---|---|---|---|
| GP0 | 1 | UART0 TX: MIDI OUT A | OUT A driver, via relay 2 |
| GP1 | 2 | UART0 RX: MIDI IN | H11L1 pin 4 (output) |
| GP2 | 4 | Bypass relay drive (HIGH = energised) | NPN base, via 1 kΩ |
| GP4 | 6 | UART1 TX: MIDI OUT B | OUT B driver |
| GP6 | 9 | I2C1 SDA | OLED SDA (later also the RTC and fuel gauge) |
| GP7 | 10 | I2C1 SCL | OLED SCL |
| GP8 | 11 | Joystick UP | joystick, other side to GND |
| GP9 | 12 | Joystick DOWN | joystick |
| GP10 | 14 | SPI1 SCK, microSD (reserved, M2) | not fitted |
| GP11 | 15 | SPI1 MOSI, microSD (reserved, M2) | not fitted |
| GP12 | 16 | SPI1 MISO, microSD (reserved, M2) | not fitted |
| GP13 | 17 | SPI1 CS, microSD (reserved, M2) | not fitted |
| GP14 | 19 | Joystick LEFT | joystick |
| GP15 | 20 | Joystick RIGHT | joystick |
| GP16 | 21 | TFT DC | TFT DC / RS |
| GP17 | 22 | TFT CS | TFT CS |
| GP18 | 24 | SPI0 SCK | TFT SCL / SCK |
| GP19 | 25 | SPI0 MOSI | TFT SDA / MOSI |
| GP20 | 26 | TFT RST | TFT RES / RST |
| GP21 | 27 | TFT backlight (PWM) | TFT BLK / BL |
| GP22 | 29 | Joystick PRESS | joystick centre push |
| GP25 | (on board) | Activity LED | blinks on every MIDI IN byte |
| GP26-28 | 31, 32, 34 | Reserved: XRC / GPI (M3) | not fitted |
| 3V3(OUT) | 36 | 3.3 V supply | opto, MIDI OUT sources, displays |
| VBUS | 40 | 5 V from USB | relay coils |
| GND | 3, 8, 13, 18, 23, 28, 33, 38 | Ground | common ground |

Free pins: GP3, GP5 (UART1 RX, unused).

## 2. MIDI IN (H11L1 opto-isolator)

Use an **H11L1**. A 6N137 needs a 5 V supply, so it is no good on a 3.3 V system.
The H11L1 has a Schmitt-trigger output that runs happily from 3.3 V.

| From | Via | To |
|---|---|---|
| MIDI IN DIN pin 4 | 220 Ω | H11L1 pin 1 (LED anode) |
| MIDI IN DIN pin 5 | (direct) | H11L1 pin 2 (LED cathode) |
| 1N4148 across H11L1 pins 1 and 2 | cathode to pin 1, anode to pin 2 | reverse-biased, protects the LED |
| H11L1 pin 6 (Vcc) | | 3V3, plus 100 nF from pin 6 to GND close to the chip |
| H11L1 pin 5 (GND) | | GND |
| H11L1 pin 4 (output) | | GP1, plus 470 Ω pull-up from pin 4 to 3V3 |
| H11L1 pin 3 | | not connected |
| MIDI IN DIN pin 2 (shield) | | **not connected** (the IN side must stay isolated) |

On the real build, pins 4 and 5 of MIDI IN go through relay 1 first (section 4).

## 3. MIDI OUT A and OUT B (3.3 V drivers)

These follow the MIDI 1.0 electrical spec update for 3.3 V transmitters (CA-033).
Build one per output:

| DIN pin | Via | From |
|---|---|---|
| 4 (source) | 33 Ω | 3V3 |
| 5 (sink) | 10 Ω | UART TX GPIO (GP0 for OUT A, GP4 for OUT B) |
| 2 (shield) | (direct) | GND |
| 1, 3 | | not connected |

OUT A goes through relay 2 (section 4). OUT B connects straight to its DIN socket.

## 4. Bypass relays (power-fail thru)

When the box is unpowered, crashed or resetting, **MIDI IN is wired straight to
OUT A** and our circuitry is fully disconnected. When the firmware is running, it
drives GP2 HIGH and the relays hand IN and OUT A over to the MCU.

### Parts

| Part | Value / type | Notes |
|---|---|---|
| RL1, RL2 | DPDT signal relay, 5 V coil (e.g. Omron G6K-2P-5VDC) | coil from VBUS (5 V) |
| Q1 | NPN, 2N3904 or BC547 | drives both coils |
| R | 1 kΩ | GP2 to Q1 base |
| R | 10 kΩ | GP2 to GND. Holds the relays off during reset. Don't rely on the RP2350 internal pull-down (erratum E9 on early silicon). |
| D1, D2 | 1N4148 | flyback diode across **each** coil, cathode to +5 V |

### Coil drive

| From | To |
|---|---|
| VBUS (5 V) | RL1 coil +, RL2 coil + |
| RL1 coil -, RL2 coil - | Q1 collector |
| Q1 emitter | GND |
| GP2 | 1 kΩ, then Q1 base |
| GP2 | 10 kΩ to GND |
| 1N4148 across each coil | cathode to coil + (5 V), anode to coil - |

### Contacts

| Relay | Pole | COM | NO (energised) | NC (unpowered) |
|---|---|---|---|---|
| RL1 (IN) | 1 | MIDI IN DIN pin 4 | 220 Ω to H11L1 pin 1 | RL2 pole 1 NC |
| RL1 (IN) | 2 | MIDI IN DIN pin 5 | H11L1 pin 2 | RL2 pole 2 NC |
| RL2 (OUT A) | 1 | OUT A DIN pin 4 | OUT A driver, 33 Ω from 3V3 | RL1 pole 1 NC |
| RL2 (OUT A) | 2 | OUT A DIN pin 5 | OUT A driver, 10 Ω from GP0 | RL1 pole 2 NC |

OUT A DIN pin 2 stays on GND in both states. MIDI IN DIN pin 2 is never connected.

```
 MIDI IN                  RL1 (IN side)                 RL2 (OUT A side)              MIDI OUT A

 DIN 4 ----------- COM1 --+-- NO1 --> 220R --> H11L1 pin 1
                          |
                          +-- NC1 ----------------------- NC1 --+
                                                                +-- COM1 ----------- DIN 4
                          3V3 --> 33R ------------------- NO1 --+

 DIN 5 ----------- COM2 --+-- NO2 --> H11L1 pin 2
                          |
                          +-- NC2 ----------------------- NC2 --+
                                                                +-- COM2 ----------- DIN 5
                          GP0 --> 10R ------------------- NO2 --+

                                                                         GND ------- DIN 2
 Coils:  VBUS (+5V) --+-- RL1 coil --+
                      |              +-- Q1 collector    Q1 emitter -- GND
                      +-- RL2 coil --+
         GP2 -- 1k -- Q1 base     GP2 -- 10k -- GND     1N4148 across each coil (cathode to +5V)

 Unpowered (NC):  IN 4 -> RL1 NC1 -> RL2 NC1 -> OUT A 4     IN 5 -> RL1 NC2 -> RL2 NC2 -> OUT A 5
 Energised (NO):  IN -> opto -> GP1 ... firmware ... GP0 -> driver -> OUT A
```

### Why two relays, not one

A single DPDT can only switch one side.

* **One relay on the IN side only.** The OUT A driver would stay wired across
  the downstream device's input while unpowered. A dead driver (pins at 0 V
  through 33 Ω and 10 Ω) loads the loop and can stop the passthrough working.
* **One relay on the OUT side only.** Our opto LED would stay in series or in
  parallel with the downstream receiver's LED. It would steal loop current
  from the upstream transmitter, and the downstream device might not see a
  clean signal.

Two relays isolate both ends. In bypass, the upstream transmitter sees exactly
one receiver (the downstream device), the same as a plain cable.

### When bypass engages

| Condition | Relays | IN to OUT A |
|---|---|---|
| No power | off | hard-wired |
| Booting (before the UARTs idle high) | off | hard-wired |
| Running | on | firmware thru (one byte time, about 320 µs) |
| core1 thru loop stalls | watchdog resets the chip within 250 ms, relays drop out | hard-wired |
| core0 UI hangs | stay on | thru keeps working on core1 |

Relay switching takes a few milliseconds. A message in flight at that moment
will be corrupted. Expect that when the box boots or reboots on a live line.

## 5. OLED (SH1106 or SSD1306, 128x64, I2C address 0x3C)

| OLED pin | Pico |
|---|---|
| VCC | 3V3(OUT), pin 36 |
| GND | GND |
| SDA | GP6, pin 9 |
| SCL | GP7, pin 10 |

Most modules have their own I2C pull-ups. If yours doesn't, add 4.7 kΩ from SDA
and from SCL to 3V3. The firmware defaults to SSD1306, which is what the
Adafruit 938 (the 1.3" OLED in the prototype order) uses. For an SH1106
module (most generic 1.3" boards), build with `-DMIDIUM_OLED_SSD1306=OFF`.
If the picture is shifted 2 pixels or has a garbage column, you have the
other chip.

**Adafruit 938 needs two changes before it will talk I2C:**

1. It ships in SPI mode. Solder-bridge jumpers **J1 and J2** on the back to
   switch it to I2C. Wire its **Data** pin to SDA and **Clk** to SCL.
2. Its **RST** pin must not float. Tie it to 3V3 through 10 kΩ, with
   100 nF from RST to GND, so it resets itself at power-up.

## 6. TFT (ST7789 1.9", 170x320, SPI)

Used in landscape (320 x 170). It is detected only when **no** OLED answers on
I2C 0x3C, so remove the OLED to use the TFT (or force it with
`-DMIDIUM_DISPLAY=2`).

| TFT pin (typical label) | Pico |
|---|---|
| VCC | 3V3(OUT), pin 36 |
| GND | GND |
| SCL / SCK | GP18, pin 24 |
| SDA / MOSI | GP19, pin 25 |
| RES / RST | GP20, pin 26 |
| DC / RS | GP16, pin 21 |
| CS | GP17, pin 22 |
| BLK / BL | GP21, pin 27 (PWM brightness) |

If the image is offset, mirrored or has red and blue swapped, adjust
`TFT_X_OFFSET`, `TFT_Y_OFFSET` or `TFT_MADCTL` in `include/config.h`.

## 7. Joystick (5-way, active low)

Each switch connects its GPIO to GND when pressed. The internal pull-ups are
enabled, so no resistors are needed.

| Direction | GPIO | Pico pin |
|---|---|---|
| UP | GP8 | 11 |
| DOWN | GP9 | 12 |
| LEFT | GP14 | 19 |
| RIGHT | GP15 | 20 |
| PRESS (centre) | GP22 | 29 |
| COM | GND | any GND |

## 8. Power (milestone 1)

| Supply | Source | Feeds |
|---|---|---|
| 5 V | USB into the Pico's micro-USB / USB-C | VBUS: relay coils (about 2 x 30 mA) |
| 3.3 V | Pico on-board regulator, 3V3(OUT) | opto, MIDI OUT sources, display |

The battery, charger and fuel gauge come in a later stage. When they do, the
5 V coils will need either a boost rail or 3 V coil relays (e.g. G6K-2P-Y
3 VDC), because a Li-ion cell can't hold a 5 V coil in.

## 9. Bench test procedure

You need a USB-MIDI interface with one IN and one OUT, a laptop running a
MIDI monitor and sender (e.g. MIDI Monitor and SysEx Librarian on macOS, or
MIDI-OX on Windows), and two 5-pin DIN cables. A second interface or a MIDI
device on OUT A makes step 3 easier.

| # | Setup | Action | Expected |
|---|---|---|---|
| 1 | Unpowered. Laptop OUT to MIDIum IN, MIDIum OUT A to a second interface IN (or a synth). | Send notes. | Notes arrive at OUT A through the relays (passive bypass). |
| 2 | Power MIDIum from USB. | Watch the relays and screen. | Relays click on after about 1 s, the splash shows, then the monitor reads `MIDIum  A:on B:on`. |
| 3 | Laptop OUT to IN. OUT A to interface 2 IN. **OUT B to the laptop's interface IN.** | Send notes, CC, PC, pitch bend and a sysex dump from the laptop. | Each appears on the MIDIum screen, the LED blinks, and identical data arrives on OUT A and OUT B. Check the laptop monitor on OUT B: the sysex is byte-identical. |
| 4 | Same. | Menu > Thru > Thru A off. Send notes. | Nothing on OUT A, OUT B still passes. Turn it back on. |
| 5 | Same. | Menu > Generate > Note test C4, Send CC, Send PC, MSC GO, MMC Play, PANIC. | The laptop sees each on OUT B, and the footer shows what was sent. MSC GO arrives as `F0 7F 7F 02 7F 01 F7`. |
| 6 | Same. | Generate > Clock BPM 120 > Clock out on. | The laptop measures 120.0 BPM (24 ppqn, 20.833 ms per clock). |
| 7 | Dense-stream test. | From the laptop, send a continuous stream on IN: a running-status CC sweep at full rate plus repeated 1 kB sysex dumps for 60 s. At the same time, run Note sweep and Clock out from the MIDIum. | The laptop on OUT B receives every input byte (compare counts or capture and diff). Generated notes and clocks are merged in without corrupting the stream: no stray data, no broken running status. Stats: `Drops out A/B` = 0, `UART errors` = 0, `Thru orphans` = 0. `Mon overflow` may count if the screen can't keep up. That affects the display only, never thru. |
| 8 | Remove USB power mid-stream. | | Relays drop out and IN goes hard-wired to OUT A again. |
