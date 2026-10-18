"""
bb_sheets.py - sheets 2 (netlist), 3 (off-board pinouts) and 4 (build order,
bench test, later stages) for the MIDIum breadboard pack.
"""
import math

from bb_model import (PARTS, WIRES, WIRE_COLOURS, hole_label, DIN_SOCKETS,
                      SW_FUNC)

# set by gen_breadboard at import time
Svg = frame = None


def _len_mm(w):
    L = 0.0
    for (ax, ay), (bx, by) in zip(w["path"], w["path"][1:]):
        L += math.hypot(bx - ax, by - ay)
    return L * 2.54


GROUPS = {
    "W01": "Power rails, board 1",
    "W05": "Power between the boards",
    "W09": "TFT (DS2)",
    "W17": "Joystick (SW1)",
    "W23": "OLED (DS1)",
    "W28": "Pico signals to board 2",
    "W32": "Relay drive",
    "W41": "MIDI IN A opto (U1)",
    "W44": "Bypass links (relay NC to NC)",
    "W46": "MIDI OUT B source",
    "W47": "DIN flying leads (off-board)",
    "W55": "MIDI IN B opto (U2, GP5) · Rev B",
    "W58": "MIDI IN B flying leads (off-board) · Rev B",
}


def checkbox(s, x, y, size=2.6):
    s.rect(x, y - size + 0.3, size, size, fill="#fff", stroke="#000", sw=0.25)


def sheet2():
    s = Svg()
    frame(s, 2, "Netlist / build table")
    s.text(14, 14.5, "Sheet 2 · Jumper list: tick each row as you fit it",
           size=4.2, weight="bold")
    s.text(14, 20.0, "Hole names: B1/B2 = board, letter + number = column + row "
           "(as printed on the board). Rails are named by what they carry; "
           "@n = the rail hole next to row n. Length = routed length on sheet 1 "
           "(cut ~10 mm longer for bends).", size=2.3, fill="#444")
    x = [13, 19, 29, 62, 99, 107, 123]
    hdr = ["", "W", "From", "To", "mm", "Colour", "Signal / purpose"]
    y = 27.5
    for xx, h in zip(x, hdr):
        s.text(xx, y, h, size=2.6, weight="bold")
    s.line(12, y + 1.3, 238, y + 1.3, sw=0.35)
    y += 4.6
    pitch = 3.3
    for w in WIRES:
        if w["id"] in GROUPS:
            s.text(x[1], y + 0.2, GROUPS[w["id"]], size=2.45, weight="bold",
                   fill="#24467a")
            y += pitch
        col, cname = WIRE_COLOURS[w["col"]]
        checkbox(s, x[0], y, 2.4)
        s.text(x[1], y, w["id"], size=2.35, weight="bold")
        s.text(x[2], y, hole_label(w["a"]), size=2.35)
        s.text(x[3], y, hole_label(w["b"]), size=2.35)
        s.text(x[4] + 5, y, f"{_len_mm(w):.0f}", size=2.35, anchor="end")
        if w["col"] == "midiinb":   # white wire: outline it
            s.line(x[5], y - 0.8, x[5] + 4.5, y - 0.8, stroke="#555", sw=1.9,
                   extra=' stroke-linecap="round"')
        s.line(x[5], y - 0.8, x[5] + 4.5, y - 0.8, stroke=col, sw=1.5,
               extra=' stroke-linecap="round"')
        short = {"3v3": "red", "5v": "orange", "gnd": "black",
                 "midiin": "green", "midiinb": "white", "outa": "blue", "outb": "violet",
                 "bypass": "brown", "relay": "yellow", "i2c": "pink",
                 "tft": "grey", "joy": "cyan"}[w["col"]]
        s.text(x[5] + 6.5, y, short, size=2.2, fill="#333")
        s.text(x[6], y, w["sig"], size=2.35)
        y += pitch
    s.line(12, y - 2.2, 238, y - 2.2, sw=0.25)

    # ------------------------------------------------------------ parts table
    X0 = 246
    s.text(X0, 27.5, "Parts on the boards", size=3.4, weight="bold")
    cx = [X0, X0 + 12, X0 + 51]
    y = 33.0
    for xx, h in zip(cx, ["Ref", "Part / value", "Pin → hole"]):
        s.text(xx, y, h, size=2.5, weight="bold")
    s.line(X0, y + 1.3, PAGE_W - 8, y + 1.3, sw=0.35)
    y += 4.6
    order = ["A1", "R3", "R4", "Q1", "D1", "RL1", "RL2", "D2", "U1", "R1",
             "D3", "C1", "R2", "U2", "R10", "D4", "C3", "R11", "R6", "R7", "R9", "R8", "DS1", "R5", "C2",
             "DS2", "SW1"]
    for ref in order:
        p = PARTS[ref]
        b = p["pins"]
        if ref == "A1":
            pins = ["B1 straddles c/h, rows 6-25, USB end off the left edge.",
                    "pin 1 GP0 = h6 ... pin 20 GP15 = h25;",
                    "pin 21 GP16 = c25 ... pin 40 VBUS = c6."]
        elif ref in ("DS1", "DS2"):
            items = [f"{n} {h.split(':')[1]}" for n, h in b.items()]
            pins = ["B1 header in column a: " + ", ".join(items[:6]),
                    "  " + ", ".join(items[6:])]
        elif ref == "SW1":
            items = [f"{n}({SW_FUNC[n]}) {h.split(':')[1]}" for n, h in b.items()]
            pins = ["B1 " + ", ".join(items[:3]), "   " + ", ".join(items[3:])]
        elif p["kind"] in ("res", "cap"):
            ends = []
            for h in b.values():
                hl = hole_label(h).replace("B2 ", "").replace("B1 ", "")
                ends.append(hl)
            pins = ["B" + list(b.values())[0].split(":")[0] + " " +
                    " \u2194 ".join(ends)]
        else:
            items = []
            for n, h in b.items():
                hl = h.split(":")[1] if ":" in h else h
                if hl[:2] in ("TP", "TG", "BP", "BG"):
                    hl = hole_label(h).replace("B2 ", "").replace("B1 ", "")
                items.append(f"{n} {hl}")
            brd = "B" + list(b.values())[0].split(":")[0] + " "
            line = brd + ", ".join(items)
            pins = [line]
            if len(line) > 58:
                half = len(items) // 2
                pins = [brd + ", ".join(items[:half]) + ",",
                        "   " + ", ".join(items[half:])]
        val = p["value"]
        if p["kind"] in ("res",):
            val = val.replace("R", " Ω") if val.endswith("R") else val
            val = f"{val} resistor"
        if p["kind"] in ("diode", "diode_up"):
            val = f"{val} diode"
        s.text(cx[0], y, ref, size=2.35, weight="bold")
        s.text(cx[1], y, val[:30], size=2.2)
        for k, line in enumerate(pins):
            s.text(cx[2], y + k * 3.3, line, size=2.2)
        note = p.get("note")
        nl = len(pins)
        if note:
            s.text(cx[1], y + 3.3, note[:34], size=1.95, fill="#555")
            nl = max(nl, 2)
        y += 3.3 * nl + 1.1
    s.text(X0, y + 2.5, "Resistors and capacitors: either way round. Diodes: "
           "A = anode, K = cathode (band).", size=2.1, fill="#444")
    s.text(X0, y + 5.8, "Relays: Omron G5V-2 terminal numbers (sheet 3). "
           "Q1: E, B, C.", size=2.1, fill="#444")
    return s.render()


PAGE_W, PAGE_H = 420.0, 297.0


# ============================================================================
# Sheet 3 - off-board pinouts
# ============================================================================
def panel(s, x, y, w, h, title):
    s.rect(x, y, w, h, fill="#fff", stroke="#999", sw=0.3, rx=1)
    s.text(x + 3, y + 5.5, title, size=3.2, weight="bold")


def din_front(s, cx, cy, R, title, pins_col=None):
    """Front (mating face) of the female socket, key notch at the top."""
    s.circle(cx, cy, R * 1.25, fill="#3a3a3a", stroke="#000", sw=0.3)
    s.circle(cx, cy, R * 1.05, fill="#1e1e1e", stroke="#666", sw=0.25)
    s.rect(cx - R * 0.17, cy - R * 1.25, R * 0.34, R * 0.32, fill="#fff",
           stroke="#000", sw=0.25)
    ang = {"2": 0, "4": 45, "1": 90, "5": -45, "3": -90}
    for p, a in ang.items():
        th = math.radians(a)
        px, py = cx + R * 0.68 * math.sin(th), cy + R * 0.68 * math.cos(th)
        col = (pins_col or {}).get(p, "#d9d9d9")
        s.circle(px, py, R * 0.13, fill=col, stroke="#fff", sw=0.25)
        lx = cx + R * 0.68 * 1.0 * math.sin(th) * 1.0
        ly = cy + R * 0.68 * math.cos(th)
        # number outside the hole, toward centre
        tx = cx + R * 0.40 * math.sin(th)
        ty = cy + R * 0.40 * math.cos(th) + 1.0
        s.text(tx, ty, p, size=2.8, anchor="middle", fill="#fff",
               weight="bold")
    s.text(cx, cy + R * 1.25 + 4.5, title, size=2.4, anchor="middle",
           weight="bold")
    s.text(cx, cy + R * 1.25 + 8.0, "key notch at top; yellow 4/5 = loop, "
           "blue 2 = shield", size=2.0, anchor="middle", fill="#444")


def din_footprint(s, cx, cy, k, mirror, title, pins_col=None):
    """Top view (mirror=False) or solder-side view (mirror=True), front down."""
    pos = {"5": (-5.0, -12.5), "4": (5.0, -12.5), "3": (-7.5, -10.0),
           "2": (0.0, -10.0), "1": (7.5, -10.0), "E": (-2.5, 0.0),
           "E ": (2.5, 0.0)}
    sg = -1 if mirror else 1
    x1, x2 = cx - 10 * k, cx + 10 * k
    yb, yf = cy - 15.2 * k, cy + 2.5 * k
    s.rect(x1, yb, 20 * k, (yf - yb), fill="#4a4a4a" if not mirror else "#e9e3d0",
           stroke="#000", sw=0.3, rx=0.5)
    s.rect(x1, yf - 1.8 * k, 20 * k, 1.8 * k,
           fill="#666" if not mirror else "#c9c1a8", stroke="none")
    s.text(cx, yf + 3.0, "\u2193 front / plug side \u2193", size=2.0,
           anchor="middle", fill="#333")
    for dx in (-7.5, 7.5):
        s.circle(cx + dx * k, cy - 5.0 * k, 1.2 * k, fill="none",
                 stroke="#999", sw=0.25)
    for p, (dx, dy) in pos.items():
        px, py = cx + sg * dx * k, cy + dy * k
        col = (pins_col or {}).get(p.strip(), "#d9d9d9")
        s.circle(px, py, 0.95 * k, fill=col, stroke="#000", sw=0.25)
        tcol = "#fff" if not mirror else "#000"
        s.text(px + 1.9 * k, py - 0.6 * k, p.strip(), size=2.6,
               anchor="middle", fill=tcol, weight="bold")
    s.text(cx, yf + 7.0, title, size=2.4, anchor="middle", weight="bold")


def sheet3():
    s = Svg()
    frame(s, 3, "Off-board pinouts")
    s.text(14, 14.5, "Sheet 3 · Pinouts for the parts that are easy to get "
           "wrong", size=4.2, weight="bold")

    # ---------------------------------------------------------------- DIN
    X, Y, W, H = 12, 19, 244, 92
    panel(s, X, Y, W, H, "5-pin DIN sockets X1-X4 · Same Sky SDS-50J "
          "(female, right-angle PCB)")
    s.text(X + 3, Y + 10, "MIDI numbering runs 1-4-2-5-3 around the arc. "
           "Pin 2 (centre) = shield.  Viewed from the front of a socket, "
           "pin 1 is on the RIGHT.", size=2.4)
    s.text(X + 3, Y + 13.6, "Pin positions taken from the Same Sky SDS-50J "
           "mechanical drawing (front view and recommended PCB layout).",
           size=2.2, fill="#555")
    cols = {"4": "#1f6fd1", "5": "#1f6fd1", "2": "#1a1a1a"}
    din_front(s, X + 36, Y + 42, 13, "FRONT (looking into the socket)",
              pins_col={"4": "#ffd84d", "5": "#ffd84d", "2": "#9fd3ff"})
    din_footprint(s, X + 92, Y + 48, 2.0, False,
                  "TOP view (component side), as on sheet 1")
    din_footprint(s, X + 150, Y + 48, 2.0, True,
                  "SOLDER side (pins seen from below)")
    # connection table
    tx, ty = X + 182, Y + 22
    s.text(tx, ty, "Lead per pin", size=2.7, weight="bold")
    rows = [("X1 MIDI IN A", [("4", "green", "B2 j19 (RL1 COM1)", "#2ca02c"),
                              ("5", "green", "B2 d19 (RL1 COM2)", "#2ca02c"),
                              ("2,1,3,E", "-", "NOT connected", None)]),
            ("X2 OUT A", [("4", "blue", "B2 d37 (RL2 COM1)", "#1f6fd1"),
                          ("5", "blue", "B2 j37 (RL2 COM2)", "#1f6fd1"),
                          ("2", "black", "B2 GND rail @39", "#1a1a1a"),
                          ("1,3", "-", "NOT connected", None)]),
            ("X3 OUT B", [("4", "violet", "B2 j51 (R8 33Ω)", "#8e44ad"),
                          ("5", "violet", "B2 j47 (R9 10Ω)", "#8e44ad"),
                          ("2", "black", "B2 GND rail @49", "#1a1a1a"),
                          ("1,3", "-", "NOT connected", None)]),
            ("X4 MIDI IN B", [("4", "white", "B2 j58 (R10 220Ω)", "#f2f2f2"),
                              ("5", "white", "B2 j56 (U2 pin 2)", "#f2f2f2"),
                              ("2,1,3,E", "-", "NOT connected", None)])]
    y = ty + 4.5
    for name, rs in rows:
        s.text(tx, y, name, size=2.4, weight="bold")
        y += 3.0
        for pin, cname, dest, col in rs:
            if col:
                if cname == "white":
                    s.line(tx + 1, y - 0.8, tx + 5, y - 0.8, stroke="#555",
                           sw=1.7, extra=' stroke-linecap="round"')
                s.line(tx + 1, y - 0.8, tx + 5, y - 0.8, stroke=col, sw=1.3,
                       extra=' stroke-linecap="round"')
            s.text(tx + 7, y, f"pin {pin}", size=2.2)
            s.text(tx + 20, y, dest, size=2.2)
            y += 2.75
        y += 0.8
    s.text(X + 3, Y + H - 4.5, "The shell tabs (E) are not used on the "
           "breadboard. Never ground a MIDI IN (A or B) shell or pin 2: the IN "
           "side must stay isolated (wiring.md section 2).", size=2.2, fill="#444")
    s.text(X + 3, Y + H - 1.6, "Socket pins are on a 2.5/5 mm grid, not 0.1\": "
           "solder short (~15 cm) solid-core leads, sleeve each joint, "
           "label the socket.", size=2.2, fill="#444")

    # ---------------------------------------------------------------- relay
    X, Y, W, H = 260, 19, 148, 92
    panel(s, X, Y, W, H, "RL1, RL2 · Omron G5V-2-H1 5 VDC (DPDT)")
    s.text(X + 3, Y + 10, "From the Omron G5V-2 datasheet, 'Terminal "
           "arrangement / internal connections (bottom view)'.", size=2.1,
           fill="#555")
    s.text(X + 3, Y + 13.2, "VERIFY against your datasheet and with a meter "
           "before fitting.", size=2.3, weight="bold", fill="#c0392b")
    # top view DIP outline
    ox, oy, p = X + 8, Y + 22, 5.0
    s.rect(ox, oy, 8 * p, 22, fill="#203f73", stroke="#000", sw=0.3, rx=0.8)
    s.rect(ox + 1.0, oy + 2, 1.0, 18, fill="#9fb6dd", stroke="none")
    pins_bot = [("1", 0), ("4", 3), ("6", 5), ("8", 7)]
    pins_top = [("16", 0), ("13", 3), ("11", 5), ("9", 7)]
    for n, k in pins_bot:
        cx = ox + p * (k + 0.5)
        s.rect(cx - 0.8, oy + 22, 1.6, 2.5, fill="#bbb", stroke="#000", sw=0.2)
        s.text(cx, oy + 28.5, n, size=2.6, anchor="middle", weight="bold")
    for n, k in pins_top:
        cx = ox + p * (k + 0.5)
        s.rect(cx - 0.8, oy - 2.5, 1.6, 2.5, fill="#bbb", stroke="#000", sw=0.2)
        s.text(cx, oy - 3.5, n, size=2.6, anchor="middle", weight="bold")
    s.text(ox + 4 * p, oy + 12.6, "TOP VIEW", size=2.6, anchor="middle",
           fill="#fff", weight="bold")
    s.text(ox + 4 * p, oy + 16.0, "stripe = coil end", size=1.9,
           anchor="middle", fill="#dfe8f7")
    s.text(ox, oy + 33, "Rows 0.3\" apart: straddles the channel like a DIP-16.",
           size=2.1)
    s.text(ox, oy + 36.4, "Pin 1 bottom-left, 16 above it (DIP numbering).",
           size=2.1)
    # schematic
    sx, sy = X + 62, Y + 22
    s.text(sx, sy - 1, "Internal (de-energised shown)", size=2.3,
           weight="bold")
    # coil
    s.rect(sx + 2, sy + 6, 6, 14, fill="none", stroke="#000", sw=0.35)
    s.line(sx + 2, sy + 20, sx + 8, sy + 6, sw=0.3)
    s.text(sx + 5, sy + 4.5, "1", size=2.3, anchor="middle")
    s.text(sx + 5, sy + 24, "16", size=2.3, anchor="middle")
    s.text(sx + 10, sy + 14, "coil", size=2.0)
    s.text(sx + 10, sy + 17, "(no", size=2.0)
    s.text(sx + 10, sy + 20, "polarity)", size=2.0)

    def pole(px, py, com, nc, no, name):
        s.circle(px, py + 8, 0.8, fill="#000")
        s.line(px, py + 8, px + 10, py + 2.6, sw=0.45)
        s.line(px + 11, py + 2, px + 18, py + 2, sw=0.35)
        s.line(px + 11, py + 14, px + 18, py + 14, sw=0.35)
        s.line(px - 6, py + 8, px, py + 8, sw=0.35)
        s.text(px - 7, py + 9, f"COM {com}", size=2.4, anchor="end",
               weight="bold")
        s.text(px + 19, py + 3, f"{nc}  NC", size=2.4, weight="bold")
        s.text(px + 19, py + 15, f"{no}  NO", size=2.4, weight="bold")
        s.text(px + 2, py + 18.5, name, size=2.0, fill="#555")
    pole(sx + 40, sy + 1, "4", "6", "8", "pole 1")
    pole(sx + 40, sy + 21, "13", "11", "9", "pole 2")
    # use table
    ty = Y + 66
    s.text(X + 3, ty, "Pin", size=2.3, weight="bold")
    s.text(X + 14, ty, "RL1 (IN side)", size=2.3, weight="bold")
    s.text(X + 72, ty, "RL2 (OUT A side, rotated 180°)", size=2.3,
           weight="bold")
    rows = [("1/16", "1 = +5V (j16), 16 = COIL-", "16 = +5V (j40), 1 = COIL-"),
            ("4 / 13", "COM: IN A DIN 4 / DIN 5", "COM: OUT A DIN 4 / DIN 5"),
            ("6 / 11", "NC: to RL2 6 / RL2 11", "NC: to RL1 6 / RL1 11"),
            ("8 / 9", "NO: R1 220Ω / opto pin 2", "NO: R6 33Ω / R7 10Ω")]
    for k, (a, b, c) in enumerate(rows):
        yy = ty + 3.6 * (k + 1)
        s.text(X + 3, yy, a, size=2.2)
        s.text(X + 14, yy, b, size=2.2)
        s.text(X + 72, yy, c, size=2.2)

    # ---------------------------------------------------------------- H11L1
    X, Y, W, H = 12, 114, 78, 62
    panel(s, X, Y, W, H, "U1 (IN A), U2 (IN B) · H11L1M, top view")
    ox, oy = X + 22, Y + 14.5
    s.rect(ox, oy, 34, 22, fill="#2b2b2b", stroke="#000", sw=0.3, rx=0.6)
    s.path(f"M {ox} {oy+8} A 3 3 0 0 1 {ox} {oy+14}", fill="#888")
    s.circle(ox + 4, oy + 18, 1.0, fill="#bbb")
    names_b = [("1", "LED A"), ("2", "LED K"), ("3", "NC")]
    names_t = [("6", "Vcc 3V3"), ("5", "GND"), ("4", "Vo (out)")]
    for k, (n, nm) in enumerate(names_b):
        cx = ox + 6 + k * 11
        s.rect(cx - 1, oy + 22, 2, 3, fill="#bbb", stroke="#000", sw=0.2)
        s.text(cx, oy + 20, n, size=2.6, anchor="middle", fill="#fff",
               weight="bold")
        s.text(cx, oy + 28.5, nm, size=2.0, anchor="middle")
    for k, (n, nm) in enumerate(names_t):
        cx = ox + 6 + k * 11
        s.rect(cx - 1, oy - 3, 2, 3, fill="#bbb", stroke="#000", sw=0.2)
        s.text(cx, oy + 4, n, size=2.6, anchor="middle", fill="#fff",
               weight="bold")
        s.text(cx, oy - 4.2, nm, size=2.0, anchor="middle")
    for k, t in enumerate([
            "Notch (pin-1 dot) LEFT. B2 rows: U1 26-28, U2 55-57.",
            "U1: 1 ← R1 220Ω ← RL1 NO1; 2 ← RL1 NO2; D3 K→1",
            "U2: 1 ← R10 220Ω ← X4 pin 4; 2 ← X4 pin 5; D4 K→1",
            "4 → GP1 (U1) / GP5 (U2) + 470Ω (R2 / R11) to 3V3",
            "6 = 3V3 + 100nF (C1 / C3) to 5 = GND; 3 open"]):
        s.text(X + 3, Y + 47.3 + k * 3.15, t, size=2.1)

    # ---------------------------------------------------------------- 2N3904
    X, Y, W, H = 93, 114, 52, 62
    panel(s, X, Y, W, H, "Q1 · 2N3904 (TO-92)")
    cx, cy = X + 26, Y + 26
    s.path(f"M {cx-9} {cy+6} A 9 9 0 0 1 {cx+9} {cy+6} Z", fill="#2b2b2b",
           stroke="#000", sw=0.3)
    s.text(cx, cy + 3, "flat face", size=2.0, anchor="middle", fill="#fff")
    s.text(cx, cy + 5.4, "towards you", size=2.0, anchor="middle", fill="#fff")
    for k, n in enumerate(("E", "B", "C")):
        x = cx - 5 + k * 5
        s.line(x, cy + 6, x, cy + 16, stroke="#888", sw=0.9)
        s.text(x, cy + 20, n, size=3.0, anchor="middle", weight="bold")
    for k, t in enumerate(["Legs spread to 0.1\": E e9, B e10, C e11.",
                           "Flat face towards the channel.",
                           "E = GND, B ← 1k ← GP2, C = COIL-."]):
        s.text(X + 3, Y + 50 + k * 3.6, t, size=2.15)

    # ---------------------------------------------------------------- nav sw
    X, Y, W, H = 148, 114, 108, 62
    panel(s, X, Y, W, H, "SW1 · Adafruit 504 5-way switch")
    ox, oy, sz = X + 8, Y + 14, 30
    s.rect(ox, oy, sz, sz, fill="#ddd", stroke="#555", sw=0.3)
    s.circle(ox + sz / 2, oy + sz / 2, 9, fill="#f4f4f4", stroke="#555", sw=0.3)
    s.rect(ox + sz / 2 - 3, oy + sz / 2 - 3, 6, 6, fill="#222")
    pins = {"6": (0.2, 0), "5": (0.5, 0), "4": (0.8, 0),
            "1": (0.2, 1), "2": (0.5, 1), "3": (0.8, 1)}
    for n, (fx, fy) in pins.items():
        px = ox + fx * sz
        py = oy - 2 if fy == 0 else oy + sz + 2
        s.circle(px, py, 1.2, fill="#aaa", stroke="#000", sw=0.2)
        s.text(px, py - 2.2 if fy == 0 else py + 4.4, n, size=2.5,
               anchor="middle", weight="bold")
    for lab, (dx, dy) in (("UP", (0, -1)), ("DOWN", (0, 1)), ("LEFT", (-1, 0)),
                          ("RIGHT", (1, 0))):
        s.text(ox + sz / 2 + dx * 12.5, oy + sz / 2 + dy * 12.0 + 1,
               lab, size=1.9, anchor="middle", fill="#333", weight="bold")
    s.text(ox + sz / 2, oy + sz + 10.5, "top view, as placed on B1 rows 48-50",
           size=1.9, anchor="middle")
    tx = X + 46
    s.text(tx, Y + 14, "Pin  ALPS  Function   GPIO   strip", size=2.2,
           weight="bold")
    rows = [("6", "Centre", "PRESS", "GP22", "d48"),
            ("5", "D", "UP", "GP8", "d49"),
            ("4", "Common", "GND", "-", "d50"),
            ("1", "A", "LEFT", "GP14", "f48"),
            ("2", "B", "DOWN", "GP9", "f49"),
            ("3", "C", "RIGHT", "GP15", "f50")]
    for k, r in enumerate(rows):
        yy = Y + 18 + k * 3.5
        for xx, v in zip((0, 6, 18, 33, 45), r):
            s.text(tx + xx, yy, v, size=2.2)
    for k, t in enumerate([
            "Pin letters per the ALPS SKQUCAA010 data sheet",
            "that Adafruit links as 'similar'. Not on a 0.1\"",
            "grid (3.25 mm): bend the legs to snap it in.",
            "VERIFY each direction with a meter to pin 4.",
            "Active low, internal pull-ups (no resistors)."]):
        s.text(tx, Y + 41 + k * 3.3, t, size=2.05,
               fill="#c0392b" if k == 3 else "#000",
               weight="bold" if k == 3 else "normal")

    # ---------------------------------------------------------------- diodes / R
    X, Y, W, H = 260, 114, 148, 62
    panel(s, X, Y, W, H, "Diodes and resistor colour codes")
    dx, dy = X + 6, Y + 13
    s.line(dx, dy + 2, dx + 30, dy + 2, stroke="#888", sw=0.6)
    s.rect(dx + 9, dy, 12, 4, fill="#f0a08a", stroke="#8a3a2a", sw=0.3, rx=1)
    s.rect(dx + 18, dy, 2, 4, fill="#111")
    s.text(dx, dy + 8.5, "A (anode)", size=2.1)
    s.text(dx + 30, dy + 8.5, "K (cathode, band)", size=2.1, anchor="end")
    for k, t in enumerate([
            "D1, D2 flyback: band (K) to +5V, i.e. B2 bottom half (g13, g42).",
            "D3 opto A protection: band (K) in j26 (U1 pin 1), stand upright.",
            "D4 opto B protection: band (K) in g55 (U2 pin 1), stand upright."]):
        s.text(X + 42, Y + 13 + k * 3.6, t, size=2.15)
    rows = [("10 Ω", "R7, R9", "brown black black"),
            ("33 Ω", "R6, R8", "orange orange black"),
            ("220 Ω", "R1, R10", "red red brown"),
            ("470 Ω", "R2, R11", "yellow violet brown"),
            ("1 kΩ", "R3", "brown black red"),
            ("10 kΩ", "R4, R5", "brown black orange"),
            ("4.7 kΩ", "(spare: I2C pull-ups)", "yellow violet red")]
    s.text(X + 3, Y + 27, "Value", size=2.3, weight="bold")
    s.text(X + 20, Y + 27, "Used for", size=2.3, weight="bold")
    s.text(X + 62, Y + 27, "4-band code (+ gold); 1% parts have 5 bands - "
           "measure", size=2.3, weight="bold")
    for k, (v, u, c) in enumerate(rows):
        yy = Y + 31 + k * 3.5
        s.text(X + 3, yy, v, size=2.2)
        s.text(X + 20, yy, u, size=2.2)
        s.text(X + 62, yy, c, size=2.2)

    # ---------------------------------------------------------------- OLED
    X, Y, W, H = 12, 179, 118, 87
    panel(s, X, Y, W, H, "DS1 · Adafruit 938 1.3\" OLED (I2C)")
    hp = ["Data", "Clk", "DC", "Rst", "CS", "3v3", "Vin", "Gnd"]
    conn = ["GP6 SDA", "GP7 SCL", "GND", "RC", "open", "open", "3V3", "GND"]
    ox, oy = X + 8, Y + 15
    s.rect(ox - 3, oy - 3, 8 * 12 + 2, 9, fill="#e8eefb", stroke="#24467a",
           sw=0.3)
    for k, (n, c) in enumerate(zip(hp, conn)):
        cx = ox + k * 12 + 4
        s.rect(cx - 1, oy, 2, 2, fill="#d4af37", stroke="#000", sw=0.2)
        s.text(cx, oy - 0.6, n, size=2.2, anchor="middle", weight="bold")
        s.text(cx, oy + 8.8, c, size=2.0, anchor="middle")
        s.text(cx, oy + 11.8, f"a{55 + k}", size=1.9, anchor="middle",
               fill="#555")
    notes = [
        ("Before fitting, look at the back of the board:", True),
        ("• J1 and J2 must be CLOSED for I2C. Older 938s ship in SPI mode", False),
        ("  (bridge both with solder). The current STEMMA QT version ships", False),
        ("  with them closed already.", False),
        ("• Address: in I2C mode the DC pin is SA0. The firmware only probes", False),
        ("  0x3C; Adafruit's 128x64 examples use 0x3D. W27 ties DC to GND", False),
        ("  to force 0x3C. Confirm with an I2C scan if it isn't detected.", False),
        ("• Rst: R5 10k to 3V3 + C2 100nF to GND (power-on reset).", False),
        ("  Harmless on the newer board, which resets itself.", False),
        ("• Power: Vin ← 3V3. The '3v3' pin is the on-board regulator", False),
        ("  OUTPUT: leave it open.  CS: leave open in I2C mode.", False),
        ("• Module pull-ups on SDA/SCL are fitted; if yours has none add", False),
        ("  4.7k from each to 3V3.  SSD1306 is the firmware default.", False),
    ]
    for k, (t, b) in enumerate(notes):
        s.text(X + 3, Y + 34 + k * 3.55, t, size=2.15,
               weight="bold" if b else "normal")

    # ---------------------------------------------------------------- TFT
    X, Y, W, H = 133, 179, 120, 87
    panel(s, X, Y, W, H, "DS2 · Adafruit 5394 1.9\" ST7789 TFT")
    tp = ["V+", "3V", "G", "CK", "SI", "SO", "TC", "RT", "DC", "CC", "BL"]
    tc = ["3V3", "open", "GND", "GP18", "GP19", "open", "GP17", "GP20", "GP16",
          "open", "GP21"]
    ox, oy = X + 6, Y + 15
    s.rect(ox - 3, oy - 3, 11 * 9.8 + 2, 9, fill="#e8eefb", stroke="#24467a",
           sw=0.3)
    for k, (n, c) in enumerate(zip(tp, tc)):
        cx = ox + k * 9.8 + 3
        s.rect(cx - 1, oy, 2, 2, fill="#d4af37", stroke="#000", sw=0.2)
        s.text(cx, oy - 0.6, n, size=2.2, anchor="middle", weight="bold")
        s.text(cx, oy + 8.8, c, size=2.0, anchor="middle")
        s.text(cx, oy + 11.8, f"a{36 + k}", size=1.9, anchor="middle",
               fill="#555")
    notes = [
        ("Label map (wiring.md → Adafruit silkscreen):", True),
        ("VCC → V+ (Vin), SCL/SCK → CK, SDA/MOSI → SI,", False),
        ("RES/RST → RT, DC/RS → DC, CS → TC, BLK/BL → BL.", False),
        ("SO (MISO) and CC (SD-card CS) are not used.", False),
        ("Header order above follows the Adafruit guide's pin list.", True),
        ("Check it against the silkscreen; if it differs, keep each", False),
        ("wire on the strip under the matching label.", False),
        ("Detection: firmware probes the OLED at 0x3C first and uses", False),
        ("the TFT only if nothing answers. Both may stay fitted; to", False),
        ("run the TFT, pull W25 (OLED Vin) or build -DMIDIUM_DISPLAY=2.", False),
        ("Offset / colour order: TFT_X_OFFSET, TFT_Y_OFFSET,", False),
        ("TFT_MADCTL in include/config.h.", False),
    ]
    for k, (t, b) in enumerate(notes):
        s.text(X + 3, Y + 34 + k * 3.55, t, size=2.15,
               weight="bold" if b else "normal")

    # ---------------------------------------------------------------- Pico/probe
    X, Y, W, H = 256, 179, 152, 58
    panel(s, X, Y, W, H, "A1 · Pico 2 pins used + Debug Probe")
    used = [("1", "GP0", "UART0 TX → OUT A", "h6"),
            ("2", "GP1", "UART0 RX ← opto A", "h7"),
            ("4", "GP2", "relay drive", "h9"),
            ("6", "GP4", "UART1 TX → OUT B", "h11"),
            ("7", "GP5", "UART1 RX ← opto B", "h12"),
            ("9/10", "GP6/7", "I2C1 SDA/SCL", "h14/15"),
            ("11/12", "GP8/9", "joy UP/DOWN", "h16/17"),
            ("19/20", "GP14/15", "joy LEFT/RIGHT", "h24/25"),
            ("21/22", "GP16/17", "TFT DC/CS", "c25/24"),
            ("24/25", "GP18/19", "SPI0 SCK/MOSI", "c22/21"),
            ("26/27", "GP20/21", "TFT RST/BL", "c20/19"),
            ("29", "GP22", "joy PRESS", "c17"),
            ("36", "3V3(OUT)", "3.3 V supply", "c10"),
            ("40", "VBUS", "5 V for relay coils", "c6")]
    for k, r in enumerate(used):
        yy = Y + 10.5 + k * 3.2
        for xx, v in zip((3, 15, 32, 66), r):
            s.text(X + xx, yy, v, size=2.1)
    px = X + 82
    for k, t in enumerate([
            "Debug Probe 'D' port → SWD header at the",
            "far end of the Pico (opposite USB):",
            "  orange SC → SWCLK, black → GND,",
            "  yellow SD → SWDIO. Use the JST-to-JST cable",
            "  (Pico 2 H) or JST-to-0.1\" female (3-pin header).",
            "The probe does not supply VBUS: keep the Pico",
            "USB plugged in or the relays never pull in.",
            "Leave the 'U' port unconnected: UART0 is MIDI.",
            "Reserved, not fitted: GP10-13 (microSD),",
            "GP26-28 (XRC/GPI), free: GP3."]):
        s.text(px, Y + 11 + k * 3.6, t, size=2.05)
    return s.render()


# ============================================================================
# Sheet 4 - build order, bench test, later stages
# ============================================================================
def sheet4():
    s = Svg()
    frame(s, 4, "Build order, bench test, later stages")
    s.text(14, 14.5, "Sheet 4 · Build order, bench test and later stages",
           size=4.2, weight="bold")
    # ----------------------------------------------------------- build order
    X, Y = 14, 23
    s.text(X, Y, "Build order (milestone 1)", size=3.4, weight="bold")
    steps = [
        ("Boards", "Meter each rail end to end; bridge any split rail at the "
         "middle. Clip B1 above B2 as on sheet 1."),
        ("Pico + rails", "Fit A1 (USB left, c/h rows 6-25). W01-W04. USB in: "
         "B1 rails read 3.3 V and GND."),
        ("Supplies to B2", "W05 (VBUS), W06-W08. Check B2: top inner 3.3 V, "
         "bottom inner 5 V, outer rails GND."),
        ("Relay drive", "R3, R4, Q1, D1, D2, RL1, RL2 (stripes as drawn), "
         "W28, W32-W40. No coil current yet (GP2 low)."),
        ("MIDI IN A", "U1 (notch left), R1, D3 upright, C1, R2, W29, W41-W43."),
        ("OUT A + bypass", "R6, R7, W30, W44, W45. Meter, unpowered: IN A DIN 4 "
         "↔ OUT A DIN 4 and 5 ↔ 5 read ~0 Ω once leads are on."),
        ("OUT B", "R8, R9 across the channel, W31, W46."),
        ("MIDI IN B", "U2 (notch left, rows 55-57), R10, D4 upright (band in "
         "g55), C3, R11, W55-W57. No relay: IN B goes straight to U2."),
        ("DIN sockets", "Solder and sleeve the leads, then W47-W54 and W58-W59 "
         "(X4 IN B). X1/X4 pin 2 and shells stay free. Rev A builds: move W54 "
         "to the GND rail @49."),
        ("OLED", "Check J1/J2, then R5, C2, DS1, W23-W27."),
        ("TFT + joystick", "DS2, W09-W16; SW1 (meter-check directions), "
         "W17-W22."),
        ("Firmware", "Hold BOOTSEL, plug USB, copy midium.uf2 (or flash "
         "with the Debug Probe). Then the bench test below."),
    ]
    y = Y + 6.5
    for k, (h, t) in enumerate(steps):
        checkbox(s, X, y, 2.8)
        s.text(X + 4.5, y, f"{k + 1}. {h}", size=2.8, weight="bold")
        ls = wrap(t, 88)
        for i, ln in enumerate(ls):
            s.text(X + 40, y + i * 3.6, ln, size=2.65)
        y += 3.6 * len(ls) + 2.0
    # ----------------------------------------------------------- bench test
    y += 4
    s.text(X, y, "Bench test (docs/wiring.md section 9)", size=3.4,
           weight="bold")
    for i, ln in enumerate(wrap("Needs: USB-MIDI interface (1 in / 1 out), a "
                                "MIDI monitor + sender on the laptop, two DIN "
                                "cables; a second interface or a synth on OUT A "
                                "helps for step 3; step 8 needs a second MIDI "
                                "source for IN B.", 110)):
        s.text(X, y + 4.8 + i * 3.4, ln, size=2.5, fill="#444")
    y += 3.4
    tests = [
        ("Unpowered. Laptop OUT → IN A, OUT A → 2nd interface/synth. "
         "Send notes.", "Notes arrive at OUT A through the relays (passive "
         "bypass)."),
        ("Power from USB. Watch relays and screen.",
         "Relays click ~1 s after power-up, splash, then 'MIDIum  A:on B:on'."),
        ("Laptop OUT → IN A, OUT A → interface 2, OUT B → laptop IN. "
         "Send notes, CC, PC, bend, sysex.",
         "Shown on screen, LED blinks, same data on A and B; sysex "
         "byte-identical on B."),
        ("Same. Menu > Thru > Thru A off. Send notes.",
         "Nothing on OUT A, OUT B still passes. Turn A back on."),
        ("Generate > Note test C4, CC, PC, MSC GO, MMC Play, PANIC.",
         "Laptop sees each on OUT B; MSC GO = F0 7F 7F 02 7F 01 F7."),
        ("Generate > Clock BPM 120 > Clock out on.",
         "120.0 BPM measured (24 ppqn, 20.833 ms per clock)."),
        ("Dense stream 60 s on IN A: running-status CC sweep + 1 kB sysex; "
         "Note sweep + Clock out running.",
         "Every byte on OUT B; Drops A/B = 0, UART errors = 0, Thru orphans "
         "= 0 (Mon overflow allowed)."),
        ("Second source into IN B. Menu > Routing > A+B to both outputs. "
         "Send from both at once, incl. sysex on each.",
         "Both streams merged on OUT A and OUT B, every message intact "
         "(sysex never interleaved). Monitor tags each line A or B."),
        ("Pull USB power mid-stream.",
         "Relays drop out; IN A hard-wired to OUT A again. IN B stops."),
    ]
    y += 11
    s.text(X + 5, y, "#", size=2.7, weight="bold")
    s.text(X + 10, y, "Setup / action", size=2.7, weight="bold")
    s.text(X + 112, y, "Expected", size=2.7, weight="bold")
    s.line(X, y + 1.2, X + 196, y + 1.2, sw=0.3)
    y += 4.6
    for k, (a, e) in enumerate(tests):
        checkbox(s, X, y, 2.8)
        s.text(X + 5, y, str(k + 1), size=2.6, weight="bold")
        lines_a = wrap(a, 58)
        lines_e = wrap(e, 50)
        for i, t in enumerate(lines_a):
            s.text(X + 10, y + i * 3.5, t, size=2.55)
        for i, t in enumerate(lines_e):
            s.text(X + 112, y + i * 3.5, t, size=2.55)
        y += 3.5 * max(len(lines_a), len(lines_e)) + 2.0

    # ----------------------------------------------------------- later stages
    X2 = 222
    Y2 = 23
    s.text(X2, Y2, "LATER · milestones 2 and 3 (not built yet)", size=3.4,
           weight="bold", fill="#b35900")
    # mini boards
    k = 1.45   # mm per 0.1"
    bx, by = X2, Y2 + 6

    def mini_board(x, y, label, rows=63, h=22):
        s.rect(x, y, rows * k + 3 * k, h * k, fill="#f4f2ec", stroke="#9a978d",
               sw=0.3, rx=1)
        s.rect(x, y + 9.85 * k, rows * k + 3 * k, 1.3 * k, fill="#dedbd2",
               stroke="none")
        s.text(x + 1, y - 1, label, size=2.3, weight="bold", fill="#555")

    def block(x, y, w, h, text, col="#ffe1c2", stroke="#b35900", dashed=True):
        extra = ' stroke-dasharray="1.6 0.9"' if dashed else ""
        s.rect(x, y, w, h, fill=col, stroke=stroke, sw=0.35, rx=0.8,
               extra=extra)
        for i, t in enumerate(text):
            s.text(x + w / 2, y + 3.4 + i * 2.9, t, size=2.1 if i else 2.3,
                   anchor="middle", weight="bold" if i == 0 else "normal")

    mini_board(bx, by + 2, "Board 1 (as built)")
    s.rect(bx + 7 * k, by + 2 + 6.4 * k, 20 * k, 8.3 * k, fill="#1f7a3a",
           stroke="none", rx=0.6)
    s.text(bx + 17 * k, by + 2 + 11.2 * k, "Pico 2", size=2.2, anchor="middle",
           fill="#fff")
    block(bx + 30 * k, by + 2 + 1 * k, 33 * k, 8 * k,
          ["TFT / joystick / OLED", "(milestone 1)"], col="#e8eefb",
          stroke="#24467a", dashed=False)
    mini_board(bx, by + 42, "Board 2 (as built)")
    block(bx + 2 * k, by + 42 + 6 * k, 55 * k, 10 * k,
          ["MIDI IN A/B, relays, OUT A/B", "(milestone 1, rows 1-58)"],
          col="#e8eefb", stroke="#24467a", dashed=False)
    block(bx + 59 * k, by + 42 + 2 * k, 6 * k, 18 * k,
          ["free", "59-", "63"], col="#fff", stroke="#999")
    mini_board(bx, by + 82, "Board 3 (LATER, half-size is enough)", rows=30)
    block(bx + 1 * k, by + 82 + 1.5 * k, 15 * k, 19 * k,
          ["RTC", "RV-3028-C8", "eval board", "I2C 0x52"])
    block(bx + 17 * k, by + 82 + 1.5 * k, 15 * k, 19 * k,
          ["microSD", "Adafruit 254", "SPI1", "GP10-13"])
    # side blocks
    block(bx + 100, by + 2, 82, 30,
          ["Battery stage (M2)", "Adafruit 4755 bq24074 charger + LiPo",
           "Adafruit 5580 MAX17048 fuel gauge, I2C 0x36",
           "charger OUT → Pico VSYS (pin 39)",
           "5 V coils won't hold on a Li-ion cell:",
           "boost rail or 3 V-coil relays (wiring.md s8)"])
    block(bx + 100, by + 42, 82, 30,
          ["XRC / GPI (M3)", "SparkFun BOB-00716 RJ45 breakout",
           "GP26/27/28 (Pico pins 31/32/34, strips c15/c14/c12)",
           "NSK XRC runs 12 V on pin 1: never wire it",
           "to the Pico directly. Isolate both ways:",
           "AQY212EH PhotoMOS out, opto in."])
    block(bx + 100, by + 82, 82, 30,
          ["GPI jack (M3)", "Adafruit 1699 3.5 mm TRS breakout",
           "AQY212EH PhotoMOS for contact-closure out",
           "opto + resistor for GPI in", "USB-C class-compliant MIDI:",
           "firmware only (Pico USB), no wiring"])
    y = by + 120
    later = [
        "Where to tap the shared I2C1 bus (GP6 SDA / GP7 SCL): B1 holes i14 "
        "and i15 (Pico strips), or the OLED strips c55 / c56.",
        "Total bus pull-up: the OLED module already has some; add 4.7k to 3V3 "
        "only if a scope shows slow edges with RTC + gauge fitted.",
        "microSD (Adafruit 254): CLK ← GP10 (i19), DI ← GP11 (i20), "
        "DO → GP12 (i21), CS ← GP13 (i22); power 5V ← VBUS, GND.",
        "RTC eval board: 3V3, GND, SDA, SCL (+ INT/CLKOUT optional). "
        "pins.h says RV-3028-C7; the part ordered is the -C8 eval board.",
        "Fuel gauge: CELL/VIN to the LiPo, GND common, SDA/SCL to the bus. "
        "ALRT optional (spare GP3).",
        "Keep relay coil current in the battery budget: about 2 x 30 mA "
        "while running.",
    ]
    for t in later:
        for i, ln in enumerate(wrap(t, 84)):
            s.text(X2, y, ("• " if i == 0 else "  ") + ln, size=2.45)
            y += 3.4
        y += 0.8
    y += 3
    s.text(X2, y, "Verify before power-up (not settled by wiring.md)", size=3.2,
           weight="bold", fill="#c0392b")
    y += 4.8
    for t in VERIFY:
        for i, ln in enumerate(wrap(t, 84)):
            s.text(X2, y, ("• " if i == 0 else "  ") + ln, size=2.45)
            y += 3.4
        y += 0.6
    return s.render()


VERIFY = [
    "Relays: wiring.md names Omron G6K-2P-5VDC; the parts ordered are "
    "G5V-2-H1 5VDC. This layout uses the G5V-2 terminal numbers (coil 1-16, "
    "COM 4/13, NC 6/11, NO 8/9). Meter COM-NC = 0 ohm unpowered.",
    "OLED address: firmware probes 0x3C only; Adafruit 938 defaults to 0x3D "
    "unless DC (SA0) is low. W27 grounds DC. J1/J2 state depends on board "
    "revision.",
    "TFT header order and joystick pin letters are from Adafruit's guide / "
    "the ALPS data sheet it cites, not from the boards in hand.",
    "Pico 2 debug connector: 3-pin 0.1\" header or JST-SH depending on the "
    "variant; pick the matching Debug Probe cable.",
]


def wrap(t, n):
    words, lines, cur = t.split(), [], ""
    for w in words:
        if len(cur) + len(w) + 1 > n and cur:
            lines.append(cur)
            cur = w
        else:
            cur = (cur + " " + w).strip()
    if cur:
        lines.append(cur)
    return lines
