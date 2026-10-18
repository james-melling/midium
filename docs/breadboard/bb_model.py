"""
bb_model.py - MIDIum (NSK MT-1) breadboard prototype: placement + wiring model.

Everything the drawings and tables show comes from this file.  The expected
netlist (EXPECTED_NETS) is transcribed independently from docs/wiring.md and
include/pins.h; gen_breadboard.py checks that the breadboard (holes + strips +
parts + wires) produces exactly those nets before it draws anything.

Coordinates are in "u" = 0.1 inch (2.54 mm).  Board 1 occupies y 0..22,
board 2 occupies y 32..54.  Row r sits at x = r + 1.5.
"""

# ----------------------------------------------------------------------------
# Breadboard geometry (Adafruit 239 full-size: 63 rows, a-j, 2 rails per side)
# ----------------------------------------------------------------------------
ROWS = 63
COLS = "abcdefghij"
COL_Y = {"a": 5, "b": 6, "c": 7, "d": 8, "e": 9,
         "f": 12, "g": 13, "h": 14, "i": 15, "j": 16}
RAIL_Y = {"TG": 1.5, "TP": 2.5, "BP": 19.5, "BG": 20.5}
BOARD_W, BOARD_H = 66.0, 22.0
BOARD_Y0 = {1: 0.0, 2: 32.0}

# What each rail carries (outer rails = GND on every side, inner = supply)
RAIL_FUNC = {
    (1, "TG"): "GND", (1, "TP"): "3V3", (1, "BP"): "3V3", (1, "BG"): "GND",
    (2, "TG"): "GND", (2, "TP"): "3V3", (2, "BP"): "5V", (2, "BG"): "GND",
}


def row_x(r):
    return r + 1.5


def rail_rows():
    """Rail holes come in groups of five: rows 3-7, 9-13, ... 57-61."""
    return [r for r in range(3, 62) if (r - 3) % 6 != 5]


RAIL_ROWS = set(rail_rows())


def parse_hole(h):
    """'1:a10' -> (1, 'a', 10); '2:TP33' -> (2, 'TP', 33)."""
    b, rest = h.split(":")
    b = int(b)
    if rest[:2] in RAIL_Y:
        return b, rest[:2], int(rest[2:])
    return b, rest[0], int(rest[1:])


def hole_xy(h):
    b, c, r = parse_hole(h)
    y0 = BOARD_Y0[b]
    if c in RAIL_Y:
        assert r in RAIL_ROWS, f"no rail hole at {h}"
        return row_x(r), y0 + RAIL_Y[c]
    return row_x(r), y0 + COL_Y[c]


def strip_of(h):
    """Electrical node a hole belongs to."""
    b, c, r = parse_hole(h)
    if c in RAIL_Y:
        return f"{b}:{c}"
    return f"{b}:{'T' if c in 'abcde' else 'B'}{r}"


def all_holes():
    out = []
    for b in (1, 2):
        for r in range(1, ROWS + 1):
            for c in COLS:
                out.append(f"{b}:{c}{r}")
        for rail in RAIL_Y:
            for r in RAIL_ROWS:
                out.append(f"{b}:{rail}{r}")
    return out


def hole_label(h):
    """Human-readable hole name for the build table."""
    if "." in h:  # off-board pin like X1.4
        ref, pin = h.split(".")
        return f"{ref} pin {pin}"
    b, c, r = parse_hole(h)
    if c in RAIL_Y:
        side = "top" if c[0] == "T" else "bottom"
        return f"B{b} {side} {RAIL_FUNC[(b, c)]} rail @{r}"
    return f"B{b} {c}{r}"


# ----------------------------------------------------------------------------
# Parts
# ----------------------------------------------------------------------------
# Pico 2 straddles board 1 with USB pointing left (off the board end).
# Pins 1-20 (GP0 side) run left->right along column h, rows 6-25.
# Pins 40-21 run left->right along column c, rows 6-25.
PICO_ROW0 = 6
PICO_PIN_NAMES = {
    1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5",
    8: "GND", 9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND",
    14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13", 18: "GND", 19: "GP14",
    20: "GP15", 21: "GP16", 22: "GP17", 23: "GND", 24: "GP18", 25: "GP19",
    26: "GP20", 27: "GP21", 28: "GND", 29: "GP22", 30: "RUN", 31: "GP26",
    32: "GP27", 33: "GND", 34: "GP28", 35: "ADC_VREF", 36: "3V3(OUT)",
    37: "3V3_EN", 38: "GND", 39: "VSYS", 40: "VBUS",
}


def pico_hole(pin):
    if pin <= 20:
        return f"1:h{PICO_ROW0 - 1 + pin}"
    return f"1:c{PICO_ROW0 + 40 - pin}"


PARTS = {}


def part(ref, kind, value, pins, **kw):
    PARTS[ref] = dict(ref=ref, kind=kind, value=value, pins=pins, **kw)


part("A1", "pico", "Raspberry Pi Pico 2",
     {str(p): pico_hole(p) for p in range(1, 41)})

# --- Board 1: displays + joystick ------------------------------------------
TFT_PINS = ["V+", "3V", "G", "CK", "SI", "SO", "TC", "RT", "DC", "CC", "BL"]
TFT_ROW0 = 36
part("DS2", "tft", "Adafruit 5394 1.9in TFT",
     {n: f"1:a{TFT_ROW0 + i}" for i, n in enumerate(TFT_PINS)})

OLED_PINS = ["Data", "Clk", "DC", "Rst", "CS", "3v3", "Vin", "Gnd"]
OLED_ROW0 = 55
part("DS1", "oled", "Adafruit 938 1.3in OLED (I2C)",
     {n: f"1:a{OLED_ROW0 + i}" for i, n in enumerate(OLED_PINS)})

# 5-way nav switch, ALPS SKQUCAA010-style pinout, rotated so that pushing
# toward the top of the drawing actuates contact D (pin 5) = UP.
#   top half (col d):    row48 pin6 Centre=PRESS, row49 pin5 D=UP, row50 pin4 COM
#   bottom half (col f): row48 pin1 A=LEFT,  row49 pin2 B=DOWN, row50 pin3 C=RIGHT
SW_ROW0 = 48
part("SW1", "nav", "Adafruit 504 nav switch",
     {"6": f"1:d{SW_ROW0}", "5": f"1:d{SW_ROW0+1}", "4": f"1:d{SW_ROW0+2}",
      "1": f"1:f{SW_ROW0}", "2": f"1:f{SW_ROW0+1}", "3": f"1:f{SW_ROW0+2}"})
SW_FUNC = {"6": "PRESS", "5": "UP", "4": "COM", "1": "LEFT", "2": "DOWN",
           "3": "RIGHT"}

part("R5", "res", "10k", {"a": "1:c58", "b": "1:c61"}, note="OLED RST pull-up")
part("C2", "cap", "100nF", {"a": "1:b58", "b": "1:b57"}, note="OLED RST to GND")

# --- Board 2: relay drive -------------------------------------------------------
part("R3", "res", "1k", {"a": "2:b6", "b": "2:b10"}, note="GP2 to Q1 base")
part("R4", "res", "10k", {"a": "2:c6", "b": "2:c9"}, note="GP2 pull-down")
part("Q1", "to92", "2N3904", {"E": "2:e9", "B": "2:e10", "C": "2:e11"})
part("D1", "diode", "1N4148", {"A": "2:d13", "K": "2:g13"}, note="RL1 flyback")
part("D2", "diode", "1N4148", {"A": "2:d42", "K": "2:g42"}, note="RL2 flyback")

# G5V-2 terminal map (datasheet bottom view: 1 4 6 8 / 16 13 11 9, top view is
# a DIP-16 with only pins 1,4,6,8,9,11,13,16).  Coil 1-16 (no polarity).
# Pole 1: COM 4, NC 6, NO 8.  Pole 2: COM 13, NC 11, NO 9.
RL1_R = 16   # normal orientation: pin 1 bottom-left (col f), pin 16 above it
part("RL1", "relay", "G5V-2-H1 5VDC",
     {"1": f"2:f{RL1_R}", "4": f"2:f{RL1_R+3}", "6": f"2:f{RL1_R+5}",
      "8": f"2:f{RL1_R+7}", "16": f"2:e{RL1_R}", "13": f"2:e{RL1_R+3}",
      "11": f"2:e{RL1_R+5}", "9": f"2:e{RL1_R+7}"}, rot=0)
RL2_R = 33   # rotated 180 deg: pin 1 top-right (col e), pin 16 below it
part("RL2", "relay", "G5V-2-H1 5VDC",
     {"8": f"2:e{RL2_R}", "6": f"2:e{RL2_R+2}", "4": f"2:e{RL2_R+4}",
      "1": f"2:e{RL2_R+7}", "9": f"2:f{RL2_R}", "11": f"2:f{RL2_R+2}",
      "13": f"2:f{RL2_R+4}", "16": f"2:f{RL2_R+7}"}, rot=180)

# --- Board 2: MIDI IN opto --------------------------------------------------------
U1_R = 26    # notch left: pins 1,2,3 bottom (col f), 6,5,4 top (col e)
part("U1", "dip6", "H11L1M",
     {"1": f"2:f{U1_R}", "2": f"2:f{U1_R+1}", "3": f"2:f{U1_R+2}",
      "6": f"2:e{U1_R}", "5": f"2:e{U1_R+1}", "4": f"2:e{U1_R+2}"})
part("R1", "res", "220R", {"a": "2:i23", "b": "2:i26"}, note="opto A LED series")
part("D3", "diode_up", "1N4148", {"K": "2:j26", "A": "2:j27"},
     note="opto A LED reverse protection")
part("C1", "cap", "100nF", {"a": "2:b26", "b": "2:b27"}, note="U1 decoupling")
part("R2", "res", "470R", {"a": "2:b28", "b": "2:TP28"}, note="opto A output pull-up")

# --- Board 2: MIDI IN B opto (Rev B) ----------------------------------------------
# Same circuit as IN A but no relay: DIN pins 4/5 go straight to R10 / U2.
# Sits in the free rows to the right of the OUT B drivers.
U2_R = 55    # notch left: pins 1,2,3 bottom (col f), 6,5,4 top (col e)
part("U2", "dip6", "H11L1M",
     {"1": f"2:f{U2_R}", "2": f"2:f{U2_R+1}", "3": f"2:f{U2_R+2}",
      "6": f"2:e{U2_R}", "5": f"2:e{U2_R+1}", "4": f"2:e{U2_R+2}"})
part("R10", "res", "220R", {"a": "2:i58", "b": "2:i55"}, note="opto B LED series")
part("D4", "diode_up", "1N4148", {"K": "2:g55", "A": "2:g56"},
     note="opto B LED reverse protection")
part("C3", "cap", "100nF", {"a": "2:b55", "b": "2:b56"}, note="U2 decoupling")
part("R11", "res", "470R", {"a": "2:c57", "b": "2:c55"}, note="opto B output pull-up")

# --- Board 2: MIDI OUT drivers -------------------------------------------------------
part("R6", "res", "33R", {"a": "2:b33", "b": "2:TP33"}, note="OUT A source")
part("R7", "res", "10R", {"a": "2:j30", "b": "2:j33"}, note="OUT A sink")
part("R8", "res", "33R", {"a": "2:d51", "b": "2:g51"}, note="OUT B source")
part("R9", "res", "10R", {"a": "2:d47", "b": "2:g47"}, note="OUT B sink")

# --- Off-board DIN sockets (Same Sky SDS-50J), drawn below board 2 ------------
# Top (component-side) view, front edge toward the bottom of the page.
# Offsets in mm from the centre between the two E (shell) pins.
DIN_PIN_MM = {"5": (-5.0, -12.5), "4": (5.0, -12.5),
              "3": (-7.5, -10.0), "2": (0.0, -10.0), "1": (7.5, -10.0),
              "E1": (-2.5, 0.0), "E2": (2.5, 0.0)}
DIN_SOCKETS = {
    "X1": dict(label="MIDI IN A", cx=20.5, cy=66.5),
    "X2": dict(label="MIDI OUT A (thru / bypass)", cx=36.5, cy=66.5),
    "X3": dict(label="MIDI OUT B", cx=50.0, cy=66.5),
    "X4": dict(label="MIDI IN B", cx=62.0, cy=66.5),
}


def din_pin_xy(ref, pin):
    s = DIN_SOCKETS[ref]
    dx, dy = DIN_PIN_MM[pin]
    return s["cx"] + dx / 2.54, s["cy"] + dy / 2.54


for ref, s in DIN_SOCKETS.items():
    part(ref, "din", "Same Sky SDS-50J", {p: f"{ref}.{p}" for p in DIN_PIN_MM},
         label=s["label"])

# ----------------------------------------------------------------------------
# Wires.  (id, from, to, colour key, signal, [via points in u])
# ----------------------------------------------------------------------------
WIRES = []


def wire(a, b, col, sig, via=None, prio=1):
    WIRES.append(dict(id=f"W{len(WIRES)+1:02d}", a=a, b=b, col=col, sig=sig,
                      via=via or [], prio=prio))


# Power, board 1
wire("1:a10", "1:TP10", "3v3", "3V3 from Pico pin 36 to B1 top 3V3 rail")
wire("1:a13", "1:TG13", "gnd", "GND from Pico pin 33 to B1 top GND rail")
wire("1:j13", "1:BG13", "gnd", "GND from Pico pin 8 to B1 bottom GND rail")
wire("1:TP61", "1:BP61", "3v3", "3V3 rail link, B1 top to B1 bottom")
# Power, board 1 -> board 2
wire("1:a6", "2:BP3", "5v", "VBUS (Pico pin 40) to B2 bottom 5V rail")
wire("1:BG57", "2:TG57", "gnd", "GND rail link, B1 bottom to B2 top")
wire("1:BP59", "2:TP59", "3v3", "3V3 rail link, B1 bottom to B2 top")
wire("2:TG61", "2:BG61", "gnd", "GND rail link, B2 top to B2 bottom",
     prio=0)  # route before the IN B leads so it keeps its straight run

# TFT (DS2)
wire("1:b36", "1:TP36", "3v3", "TFT V+ (Vin) 3V3")
wire("1:b38", "1:TG37", "gnd", "TFT G (GND)")
wire("1:b19", "1:c46", "tft", "GP21 to TFT BL (backlight PWM)")
wire("1:b20", "1:d43", "tft", "GP20 to TFT RT (reset)")
wire("1:b21", "1:e40", "tft", "GP19 SPI0 MOSI to TFT SI")
wire("1:b22", "1:e39", "tft", "GP18 SPI0 SCK to TFT CK")
wire("1:b24", "1:e42", "tft", "GP17 to TFT TC (chip select)")
wire("1:b25", "1:e44", "tft", "GP16 to TFT DC")

# Joystick (SW1)
wire("1:b17", "1:b48", "joy", "GP22 to joystick PRESS (pin 6)")
wire("1:j16", "1:b49", "joy", "GP8 to joystick UP (pin 5)")
wire("1:b50", "1:TG49", "gnd", "Joystick COM (pin 4) to GND")
wire("1:j24", "1:j48", "joy", "GP14 to joystick LEFT (pin 1)")
wire("1:j17", "1:j49", "joy", "GP9 to joystick DOWN (pin 2)")
wire("1:j25", "1:j50", "joy", "GP15 to joystick RIGHT (pin 3)")

# OLED (DS1)
wire("1:j14", "1:e55", "i2c", "GP6 I2C1 SDA to OLED Data")
wire("1:j15", "1:e56", "i2c", "GP7 I2C1 SCL to OLED Clk")
wire("1:b61", "1:TP60", "3v3", "OLED Vin 3V3")
wire("1:b62", "1:TG61", "gnd", "OLED Gnd")
wire("1:d57", "1:d62", "gnd", "OLED DC (= SA0) to GND, sets address 0x3C")

# Pico -> board 2 signals
wire("1:j9", "2:a6", "relay", "GP2 bypass relay drive")
wire("1:j7", "2:c28", "midiin", "GP1 UART0 RX from opto A (U1) output")
wire("1:j6", "2:i30", "outa", "GP0 UART0 TX (OUT A)")
wire("1:j11", "2:a47", "outb", "GP4 UART1 TX (OUT B)")

# Relay drive, board 2
wire("2:a9", "2:TG9", "gnd", "Q1 emitter to GND")
wire("2:a11", "2:a13", "relay", "COIL- : Q1 collector to D1 anode")
wire("2:b13", "2:b16", "relay", "COIL- : D1 anode to RL1 pin 16")
wire("2:a16", "2:a40", "relay", "COIL- : RL1 pin 16 to RL2 pin 1")
wire("2:b40", "2:b42", "relay", "COIL- : RL2 pin 1 to D2 anode")
wire("2:j13", "2:BP13", "5v", "D1 cathode to 5V")
wire("2:j16", "2:BP16", "5v", "RL1 coil + (pin 1) to 5V")
wire("2:j40", "2:BP40", "5v", "RL2 coil + (pin 16) to 5V")
wire("2:j42", "2:BP42", "5v", "D2 cathode to 5V")

# MIDI IN / opto
wire("2:c23", "2:g27", "midiin", "RL1 NO2 (pin 9) to H11L1 pin 2")
wire("2:a26", "2:TP27", "3v3", "H11L1 pin 6 Vcc to 3V3")
wire("2:a27", "2:TG27", "gnd", "H11L1 pin 5 to GND")

# Bypass (NC) links between relays
wire("2:g21", "2:d35", "bypass", "BYPASS 4: RL1 NC1 (pin 6) to RL2 NC1 (pin 6)")
wire("2:a21", "2:g35", "bypass", "BYPASS 5: RL1 NC2 (pin 11) to RL2 NC2 (pin 11)")

# OUT B source feed
wire("2:a51", "2:TP51", "3v3", "3V3 to R8 (OUT B source)")

# DIN flying leads
wire("X1.4", "2:j19", "midiin", "MIDI IN A DIN pin 4 to RL1 COM1 (pin 4)")
wire("X1.5", "2:d19", "midiin", "MIDI IN A DIN pin 5 to RL1 COM2 (pin 13)")
wire("X2.4", "2:d37", "outa", "OUT A DIN pin 4 to RL2 COM1 (pin 4)")
wire("X2.5", "2:j37", "outa", "OUT A DIN pin 5 to RL2 COM2 (pin 13)")
wire("X2.2", "2:BG39", "gnd", "OUT A DIN pin 2 (shield) to GND")
wire("X3.4", "2:j51", "outb", "OUT B DIN pin 4 to R8 33R")
wire("X3.5", "2:j47", "outb", "OUT B DIN pin 5 to R9 10R")
wire("X3.2", "2:BG49", "gnd", "OUT B DIN pin 2 (shield) to GND (Rev B: was @53)")

# MIDI IN B (Rev B).  Appended so that the Rev A wire numbers W01-W54 stay put.
wire("1:j12", "2:a57", "midiinb", "GP5 UART1 RX from opto B (U2) output")
wire("2:a55", "2:TP55", "3v3", "H11L1 U2 pin 6 Vcc to 3V3")
wire("2:a56", "2:TG55", "gnd", "H11L1 U2 pin 5 to GND")
wire("X4.4", "2:j58", "midiinb", "MIDI IN B DIN pin 4 to R10 220R")
wire("X4.5", "2:j56", "midiinb", "MIDI IN B DIN pin 5 to H11L1 U2 pin 2")

WIRE_COLOURS = {
    "3v3": ("#d62728", "3V3"),
    "5v": ("#ff8c00", "5V (VBUS)"),
    "gnd": ("#1a1a1a", "GND"),
    "midiin": ("#2ca02c", "MIDI IN A path (DIN IN A, U1, GP1)"),
    "midiinb": ("#f2f2f2", "MIDI IN B path (DIN IN B, U2, GP5)"),
    "outa": ("#1f6fd1", "MIDI OUT A (GP0, DIN OUT A)"),
    "outb": ("#8e44ad", "MIDI OUT B (GP4, DIN OUT B)"),
    "bypass": ("#8c564b", "Bypass links (relay NC to NC)"),
    "relay": ("#e6c200", "Relay drive (GP2, coil -)"),
    "i2c": ("#e377c2", "I2C1 to OLED (GP6 SDA, GP7 SCL)"),
    "tft": ("#7f7f7f", "SPI0 / control to TFT"),
    "joy": ("#17becf", "Joystick (GP8/9/14/15/22)"),
}

# ----------------------------------------------------------------------------
# Expected netlist, transcribed from docs/wiring.md (sections 1-8, Rev B:
# MIDI IN B on U2 / GP5 per sections 1 and 2; wiring.md wins over pins.h).
# Component pin = "REF.PIN".  Pico GND pins are joined on the Pico itself.
# ----------------------------------------------------------------------------
EXPECTED_NETS = {
    "3V3": ["A1.36", "U1.6", "C1.a", "R2.b", "R6.b", "R8.a", "DS1.Vin",
            "R5.b", "DS2.V+", "U2.6", "C3.a", "R11.b"],
    "GND": ["A1.33", "A1.38", "A1.3", "A1.8", "A1.13", "A1.18", "A1.23",
            "A1.28", "U1.5", "C1.b", "Q1.E", "R4.b", "X2.2", "X3.2",
            "DS1.Gnd", "DS1.DC", "C2.b", "DS2.G", "SW1.4", "U2.5", "C3.b"],
    "5V": ["A1.40", "RL1.1", "RL2.16", "D1.K", "D2.K"],
    "COIL-": ["Q1.C", "RL1.16", "RL2.1", "D1.A", "D2.A"],
    "GP2": ["A1.4", "R3.a", "R4.a"],
    "Q1 BASE": ["R3.b", "Q1.B"],
    "IN A DIN4": ["X1.4", "RL1.4"],
    "IN A DIN5": ["X1.5", "RL1.13"],
    "RL1 NO1": ["RL1.8", "R1.a"],
    "U1 LED A": ["R1.b", "U1.1", "D3.K"],
    "U1 LED K": ["RL1.9", "U1.2", "D3.A"],
    "BYPASS 4": ["RL1.6", "RL2.6"],
    "BYPASS 5": ["RL1.11", "RL2.11"],
    "OUT A DIN4": ["X2.4", "RL2.4"],
    "OUT A DIN5": ["X2.5", "RL2.13"],
    "OUT A SRC": ["RL2.8", "R6.a"],
    "OUT A SNK": ["RL2.9", "R7.b"],
    "GP0": ["A1.1", "R7.a"],
    "GP1": ["A1.2", "U1.4", "R2.a"],
    "GP4": ["A1.6", "R9.a"],
    "IN B DIN4": ["X4.4", "R10.a"],
    "U2 LED A": ["R10.b", "U2.1", "D4.K"],
    "U2 LED K": ["X4.5", "U2.2", "D4.A"],
    "GP5": ["A1.7", "U2.4", "R11.a"],
    "OUT B DIN5": ["R9.b", "X3.5"],
    "OUT B DIN4": ["R8.b", "X3.4"],
    "SDA": ["A1.9", "DS1.Data"],
    "SCL": ["A1.10", "DS1.Clk"],
    "OLED RST": ["DS1.Rst", "R5.a", "C2.a"],
    "TFT CK": ["A1.24", "DS2.CK"],
    "TFT SI": ["A1.25", "DS2.SI"],
    "TFT RT": ["A1.26", "DS2.RT"],
    "TFT DC": ["A1.21", "DS2.DC"],
    "TFT TC": ["A1.22", "DS2.TC"],
    "TFT BL": ["A1.27", "DS2.BL"],
    "JOY UP": ["A1.11", "SW1.5"],
    "JOY DOWN": ["A1.12", "SW1.2"],
    "JOY LEFT": ["A1.19", "SW1.1"],
    "JOY RIGHT": ["A1.20", "SW1.3"],
    "JOY PRESS": ["A1.29", "SW1.6"],
}
PICO_GND_PINS = ["A1.3", "A1.8", "A1.13", "A1.18", "A1.23", "A1.28",
                 "A1.33", "A1.38"]
