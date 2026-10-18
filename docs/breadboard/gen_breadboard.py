#!/usr/bin/env python3
"""
gen_breadboard.py - builds the MIDIum breadboard layout sheets.

    python3 gen_breadboard.py            # writes SVGs + PDF next to this file

Outputs (in this directory):
    MIDIum_breadboard_p1_layout.svg
    MIDIum_breadboard_p2_netlist.svg
    MIDIum_breadboard_p3_pinouts.svg
    MIDIum_breadboard_p4_build.svg
    MIDIum_breadboard_layout.pdf     (A3 landscape, 4 pages)

Source of truth for connections: ../wiring.md and ../../include/pins.h,
transcribed into bb_model.EXPECTED_NETS.  The script refuses to draw anything
if the breadboard model does not produce exactly those nets.
"""
import html
import math
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from bb_model import (PARTS, WIRES, WIRE_COLOURS, RAIL_Y, RAIL_ROWS, COL_Y,
                      COLS, ROWS, BOARD_W, BOARD_H, BOARD_Y0, RAIL_FUNC,
                      row_x, hole_xy, hole_label, din_pin_xy, DIN_SOCKETS,
                      SW_FUNC, PICO_PIN_NAMES, EXPECTED_NETS)
import bb_route
from bb_route import module_rect, part_xy

FONT = "'Century Gothic','URW Gothic','URW Gothic L','Questrial',sans-serif"
TITLE = "MIDIum (NSK MT-1) · Breadboard prototype · Rev A · 18/10/2026 · J Melling"
PAGE_W, PAGE_H = 420.0, 297.0

# drawing scale on sheet 1: mm per 0.1 inch
U = 3.2
OX = 12.0 + 3.0 * U      # page mm of u = 0
OY = 13.0 + 11.0 * U


def esc(s):
    return html.escape(str(s), quote=True)


class Svg:
    def __init__(self):
        self.out = []

    def add(self, s):
        self.out.append(s)

    def text(self, x, y, s, size=2.5, anchor="start", weight="normal",
             fill="#111", rotate=None, italic=False, extra=""):
        tr = f' transform="rotate({rotate} {x:.2f} {y:.2f})"' if rotate else ""
        st = ' font-style="italic"' if italic else ""
        self.add(f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size:.2f}" '
                 f'text-anchor="{anchor}" font-weight="{weight}" fill="{fill}"'
                 f'{st}{tr}{extra}>{esc(s)}</text>')

    def rect(self, x, y, w, h, fill="none", stroke="#000", sw=0.25, rx=0,
             extra=""):
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" '
                 f'height="{h:.2f}" rx="{rx:.2f}" fill="{fill}" '
                 f'stroke="{stroke}" stroke-width="{sw:.2f}"{extra}/>')

    def line(self, x1, y1, x2, y2, stroke="#000", sw=0.25, extra=""):
        self.add(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" '
                 f'y2="{y2:.2f}" stroke="{stroke}" stroke-width="{sw:.2f}"'
                 f'{extra}/>')

    def circle(self, x, y, r, fill="none", stroke="#000", sw=0.25, extra=""):
        self.add(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}" '
                 f'fill="{fill}" stroke="{stroke}" stroke-width="{sw:.2f}"'
                 f'{extra}/>')

    def poly(self, pts, stroke="#000", sw=0.25, fill="none", extra=""):
        p = " ".join(f"{x:.2f},{y:.2f}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw:.2f}"{extra}/>')

    def polygon(self, pts, stroke="#000", sw=0.25, fill="none", extra=""):
        p = " ".join(f"{x:.2f},{y:.2f}" for x, y in pts)
        self.add(f'<polygon points="{p}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw:.2f}"{extra}/>')

    def path(self, d, stroke="#000", sw=0.25, fill="none", extra=""):
        self.add(f'<path d="{d}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw:.2f}"{extra}/>')

    def render(self):
        body = "\n".join(self.out)
        return (f'<svg xmlns="http://www.w3.org/2000/svg" '
                f'width="{PAGE_W}mm" height="{PAGE_H}mm" '
                f'viewBox="0 0 {PAGE_W} {PAGE_H}" font-family="{FONT}">\n'
                f'<rect x="0" y="0" width="{PAGE_W}" height="{PAGE_H}" fill="#fff"/>\n'
                f'{body}\n</svg>\n')


def P(x, y):
    """u -> page mm (sheet 1)."""
    return OX + x * U, OY + y * U


# ----------------------------------------------------------------------------
# Frame + title block (all sheets)
# ----------------------------------------------------------------------------
def frame(s, sheet, title):
    s.rect(6, 6, PAGE_W - 12, PAGE_H - 12, sw=0.5)
    # title block, bottom right
    x0, y0, w, h = PAGE_W - 6 - 150, PAGE_H - 6 - 24, 150, 24
    s.rect(x0, y0, w, h, fill="#fff", sw=0.5)
    s.line(x0, y0 + 9, x0 + w, y0 + 9, sw=0.3)
    s.line(x0, y0 + 16.5, x0 + w, y0 + 16.5, sw=0.3)
    s.line(x0 + 112, y0 + 9, x0 + 112, y0 + h, sw=0.3)
    s.text(x0 + 3, y0 + 6.3, "MIDIum (NSK MT-1)", size=5, weight="bold")
    s.text(x0 + w - 3, y0 + 6.3, "melling.audio", size=3.2, anchor="end",
           fill="#555")
    s.text(x0 + 3, y0 + 14.2, f"Breadboard prototype · {title}", size=3.4)
    s.text(x0 + 3, y0 + 21.6, "Rev A · 18/10/2026 · J Melling", size=3.2)
    s.text(x0 + 115, y0 + 14.2, f"Sheet {sheet} of 4", size=3.2)
    s.text(x0 + 115, y0 + 21.6, "A3 · milestone 1", size=3.0, fill="#444")


# ----------------------------------------------------------------------------
# Sheet 1 drawing primitives
# ----------------------------------------------------------------------------
def draw_board(s, b):
    y0 = BOARD_Y0[b]
    x, y = P(0, y0)
    s.rect(x, y, BOARD_W * U, BOARD_H * U, fill="#f4f2ec", stroke="#9a978d",
           sw=0.35, rx=1.5)
    # centre channel
    cx, cy = P(0.4, y0 + 9.85)
    s.rect(cx, cy, (BOARD_W - 0.8) * U, 1.3 * U, fill="#dedbd2", stroke="none")
    # rail stripes
    for ry, col in ((0.75, "#2f6fd6"), (3.25, "#d62728"), (18.75, "#d62728"),
                    (21.25, "#2f6fd6")):
        a = P(3.6, y0 + ry)
        bb = P(62.4, y0 + ry)
        s.line(a[0], a[1], bb[0], bb[1], stroke=col, sw=0.35)
    # holes
    hs = 0.52 * U
    for r in range(1, ROWS + 1):
        for c in COLS:
            hx, hy = P(row_x(r), y0 + COL_Y[c])
            s.rect(hx - hs / 2, hy - hs / 2, hs, hs, fill="#3d3d3d",
                   stroke="none", rx=0.2)
    for rail, ry in RAIL_Y.items():
        for r in RAIL_ROWS:
            hx, hy = P(row_x(r), y0 + ry)
            s.rect(hx - hs / 2, hy - hs / 2, hs, hs, fill="#3d3d3d",
                   stroke="none", rx=0.2)
    # rail function labels (both ends)
    for rail, ry in RAIL_Y.items():
        f = RAIL_FUNC[(b, rail)]
        sign = "−" if f == "GND" else "+"
        col = "#2f6fd6" if f == "GND" else ("#ff8c00" if f == "5V" else "#d62728")
        for xx, anc in ((2.9, "end"), (63.1, "start")):
            tx, ty = P(xx, y0 + ry + 0.32)
            s.text(tx, ty, f"{f} {sign}" if anc == "end" else f"{sign} {f}",
                   size=1.25, anchor=anc, fill=col, weight="bold")
    # column letters
    for c in COLS:
        for xx in (0.95, 65.05):
            tx, ty = P(xx, y0 + COL_Y[c] + 0.3)
            s.text(tx, ty, c, size=1.3, anchor="middle", fill="#777")
    # row numbers
    for r in range(1, ROWS + 1):
        bold = r % 5 == 0 or r == 1
        for yy in (4.05, 17.55):
            if b == 1 and yy < 10 and (36 <= r <= 46 or 55 <= r <= 62):
                continue
            tx, ty = P(row_x(r), y0 + yy)
            s.text(tx, ty, str(r), size=1.1 if not bold else 1.25,
                   anchor="middle", fill="#555" if bold else "#999",
                   weight="bold" if bold else "normal")
    # board name
    tx, ty = P(-1.6, y0 + 11)
    s.text(tx, ty, f"BOARD {b}", size=3.0, anchor="middle", weight="bold",
           rotate=-90, fill="#555")


def lead(s, a, b, sw=0.22):
    s.line(*P(*a), *P(*b), stroke="#8a8a8a", sw=sw * U)


RES_BANDS = {  # value -> 4-band colours (1%: use 4-band for readability)
    "10R": ["#8b4513", "#000000", "#000000", "#d4af37"],
    "33R": ["#ff8c00", "#ff8c00", "#000000", "#d4af37"],
    "220R": ["#d62728", "#d62728", "#8b4513", "#d4af37"],
    "470R": ["#ffd700", "#7b2fbf", "#8b4513", "#d4af37"],
    "1k": ["#8b4513", "#000000", "#d62728", "#d4af37"],
    "10k": ["#8b4513", "#000000", "#ff8c00", "#d4af37"],
}


def draw_axial(s, ref, kind, a, b, value):
    ax, ay = a
    bx, by = b
    lead(s, a, b)
    L = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / L, (by - ay) / L
    mx, my = (ax + bx) / 2, (ay + by) / 2
    ang = math.degrees(math.atan2(by - ay, bx - ax))
    cx, cy = P(mx, my)
    if kind == "res":
        bl, bw = min(2.5, L - 0.4), 0.8
        g = (f'<g transform="translate({cx:.2f} {cy:.2f}) rotate({ang:.1f})">')
        g += (f'<rect x="{-bl*U/2:.2f}" y="{-bw*U/2:.2f}" width="{bl*U:.2f}" '
              f'height="{bw*U:.2f}" rx="{0.3*U:.2f}" fill="#e8d3a3" '
              f'stroke="#7a6a48" stroke-width="0.2"/>')
        bands = RES_BANDS[value]
        for k, col in enumerate(bands):
            bxp = (-bl / 2 + 0.45 + k * 0.42 + (0.35 if k == 3 else 0)) * U
            g += (f'<rect x="{bxp:.2f}" y="{-bw*U/2:.2f}" width="{0.22*U:.2f}" '
                  f'height="{bw*U:.2f}" fill="{col}"/>')
        g += "</g>"
        s.add(g)
    elif kind == "cap":
        s.add(f'<g transform="translate({cx:.2f} {cy:.2f}) rotate({ang:.1f})">'
              f'<ellipse cx="0" cy="0" rx="{0.62*U:.2f}" ry="{0.42*U:.2f}" '
              f'fill="#f2b33d" stroke="#8a5a00" stroke-width="0.2"/>'
              f'<text x="0" y="{0.17*U:.2f}" font-size="{0.42*U:.2f}" '
              f'text-anchor="middle" fill="#5a3a00">104</text></g>')
    elif kind == "diode":
        bl, bw = 1.6, 0.75
        # cathode band at the K end (b)
        s.add(f'<g transform="translate({cx:.2f} {cy:.2f}) rotate({ang:.1f})">'
              f'<rect x="{-bl*U/2:.2f}" y="{-bw*U/2:.2f}" width="{bl*U:.2f}" '
              f'height="{bw*U:.2f}" rx="{0.3*U:.2f}" fill="#f0a08a" '
              f'fill-opacity="0.85" stroke="#8a3a2a" stroke-width="0.2"/>'
              f'<rect x="{(bl/2-0.38)*U:.2f}" y="{-bw*U/2:.2f}" '
              f'width="{0.22*U:.2f}" height="{bw*U:.2f}" fill="#111"/></g>')


def draw_part(s, ref, p):
    k = p["kind"]
    if k in ("res", "cap"):
        a, b = part_xy(ref, "a"), part_xy(ref, "b")
        draw_axial(s, ref, k, a, b, p["value"])
    elif k == "diode":
        draw_axial(s, ref, "diode", part_xy(ref, "A"), part_xy(ref, "K"),
                   p["value"])
    elif k == "diode_up":
        kx, ky = part_xy(ref, "K")
        axx, ayy = part_xy(ref, "A")
        # body stands on the K hole, A lead loops over to the next hole
        s.path(f"M {P(kx,ky)[0]:.2f} {P(kx,ky)[1]:.2f} "
               f"Q {P((kx+axx)/2, ky-1.0)[0]:.2f} {P((kx+axx)/2, ky-1.0)[1]:.2f} "
               f"{P(axx,ayy)[0]:.2f} {P(axx,ayy)[1]:.2f}",
               stroke="#8a8a8a", sw=0.2 * U)
        cx, cy = P(kx, ky)
        s.circle(cx, cy, 0.45 * U, fill="#f0a08a", stroke="#8a3a2a", sw=0.2)
        s.circle(cx, cy, 0.25 * U, fill="#111", stroke="none")
    elif k == "to92":
        xs = [part_xy(ref, q)[0] for q in ("E", "B", "C")]
        y = part_xy(ref, "E")[1]
        x1, x2 = min(xs) - 0.95, max(xs) + 0.95
        yf = y + 0.42     # flat face (towards the channel / viewer)
        a, b = P(x1, yf), P(x2, yf)
        rx = (x2 - x1) / 2 * U
        s.path(f"M {a[0]:.2f} {a[1]:.2f} A {rx:.2f} {1.45*U:.2f} 0 0 1 "
               f"{b[0]:.2f} {b[1]:.2f} Z", fill="#2b2b2b", stroke="#000",
               sw=0.2)
        for q, xq in zip(("E", "B", "C"), xs):
            tx, ty = P(xq, y - 0.45)
            s.text(tx, ty, q, size=1.15, anchor="middle", fill="#fff",
                   weight="bold")
            hx, hy = P(xq, y)
            s.circle(hx, hy, 0.18 * U, fill="#c0c0c0", stroke="none")
    elif k == "relay":
        pins = p["pins"]
        xs = [hole_xy(h)[0] for h in pins.values()]
        ys = [hole_xy(h)[1] for h in pins.values()]
        cy = (min(ys) + max(ys)) / 2
        x1, x2 = min(xs) - 0.53, max(xs) + 0.53
        a = P(x1, cy - 1.99)
        s.rect(a[0], a[1], (x2 - x1) * U, 3.98 * U, fill="#203f73",
               stroke="#0d1d3a", sw=0.3, rx=0.6)
        # orientation stripe at the coil (pin 1/16) end
        x16 = hole_xy(pins["16"])[0]
        sx = x16 - 0.25 if p["rot"] == 0 else x16 - 0.35
        st = P(sx + (0.05 if p["rot"] == 0 else 0.4), cy - 1.6)
        s.rect(st[0], st[1], 0.22 * U, 3.2 * U, fill="#9fb6dd", stroke="none")
        for pin, h in pins.items():
            hx, hy = hole_xy(h)
            c = P(hx, hy)
            s.circle(*c, 0.2 * U, fill="#d9d9d9", stroke="none")
            tx, tyy = P(hx, hy + 1.05 if (hy - 32) < 10.5 else hy - 0.55)
            s.text(tx, tyy, pin, size=1.25, anchor="middle", fill="#ffe680",
                   weight="bold")
        tx, ty = P((x1 + x2) / 2, cy + 0.33)
        s.text(tx, ty, f"{ref} · G5V-2-H1 5V", size=1.45, anchor="middle",
               fill="#fff", weight="bold")
    elif k == "dip6":
        pins = p["pins"]
        xs = [hole_xy(h)[0] for h in pins.values()]
        ys = [hole_xy(h)[1] for h in pins.values()]
        cy = (min(ys) + max(ys)) / 2
        x1, x2 = min(xs) - 0.75, max(xs) + 0.75
        a = P(x1, cy - 1.28)
        s.rect(a[0], a[1], (x2 - x1) * U, 2.56 * U, fill="#2b2b2b",
               stroke="#000", sw=0.3, rx=0.3)
        n = P(x1, cy)
        s.path(f"M {n[0]:.2f} {n[1]-0.45*U:.2f} A {0.45*U:.2f} {0.45*U:.2f} 0 0 1 "
               f"{n[0]:.2f} {n[1]+0.45*U:.2f}", fill="#777", stroke="none")
        dot = P(x1 + 0.45, cy + 0.85)
        s.circle(*dot, 0.15 * U, fill="#bbb", stroke="none")
        for pin, h in pins.items():
            hx, hy = hole_xy(h)
            s.circle(*P(hx, hy), 0.18 * U, fill="#c0c0c0", stroke="none")
            tx, tyy = P(hx, hy - 0.4 if hy - 32 > 10.5 else hy + 0.75)
            s.text(tx, tyy, pin, size=1.1, anchor="middle", fill="#ffe680",
                   weight="bold")
        tx, ty = P((x1 + x2) / 2 + 0.2, cy + 0.3)
        s.text(tx, ty, "U1 H11L1M", size=1.1, anchor="middle", fill="#fff",
               weight="bold")
    elif k == "nav":
        pins = p["pins"]
        xs = [hole_xy(h)[0] for h in pins.values()]
        ys = [hole_xy(h)[1] for h in pins.values()]
        cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        a = P(cx - 1.97, cy - 1.97)
        s.rect(a[0], a[1], 3.94 * U, 3.94 * U, fill="#cfcfcf", stroke="#555",
               sw=0.3, rx=0.3)
        s.circle(*P(cx, cy), 1.25 * U, fill="#e8e8e8", stroke="#555", sw=0.25)
        s.rect(*P(cx - 0.63, cy - 0.63), 1.26 * U, 1.26 * U, fill="#222",
               stroke="none", rx=0.2)
        for pin, h in pins.items():
            hx, hy = hole_xy(h)
            s.circle(*P(hx, hy), 0.18 * U, fill="#888", stroke="none")
        # direction arrows (as wired)
        for lab, (dx, dy) in (("UP", (0, -1)), ("DN", (0, 1)), ("L", (-1, 0)),
                              ("R", (1, 0))):
            tx, ty = P(cx + dx * 1.45, cy + dy * 1.45 + 0.3)
            s.text(tx, ty, lab, size=0.95, anchor="middle", fill="#333",
                   weight="bold")
    elif k == "pico":
        draw_pico(s)
    elif k == "din":
        pass  # drawn separately


def draw_pico(s):
    x1, x2, y1, y2 = 6.96, 27.04, 6.37, 14.63
    a = P(x1, y1)
    s.rect(a[0], a[1], (x2 - x1) * U, (y2 - y1) * U, fill="#1f7a3a",
           stroke="#0e3d1d", sw=0.35, rx=0.8)
    # USB connector (micro-USB) overhanging the left end
    u = P(6.2, 9.2)
    s.rect(u[0], u[1], 1.6 * U, 2.6 * U, fill="#c9c9c9", stroke="#666", sw=0.25,
           rx=0.2)
    # RP2350 + flash outlines
    c = P(14.2, 8.9)
    s.rect(c[0], c[1], 3.2 * U, 3.2 * U, fill="#2b2b2b", stroke="none", rx=0.2)
    s.text(*P(15.8, 10.75), "RP2350", size=1.1, anchor="middle", fill="#ddd")
    # BOOTSEL button
    s.circle(*P(10.2, 10.0), 0.55 * U, fill="#eee", stroke="#999", sw=0.2)
    s.text(*P(10.2, 11.4), "BOOTSEL", size=0.85, anchor="middle", fill="#ddd")
    # LED GP25
    s.rect(*P(8.0, 9.2), 0.7 * U, 0.45 * U, fill="#7fff7f", stroke="none")
    s.text(*P(8.35, 10.3), "LED", size=0.8, anchor="middle", fill="#ddd")
    # SWD pads at the far end
    for k, lab in enumerate(("SWDIO", "GND", "SWCLK")):
        yy = 9.5 + k
        s.circle(*P(26.4, yy), 0.22 * U, fill="#d4af37", stroke="none")
        s.text(*P(25.85, yy + 0.25), lab, size=0.8, anchor="end", fill="#fff")
    # pins + labels
    for pin in range(1, 41):
        hx, hy = hole_xy(PARTS["A1"]["pins"][str(pin)])
        s.rect(*P(hx - 0.3, hy - 0.3), 0.6 * U, 0.6 * U, fill="#d4af37",
               stroke="#8a6d1a", sw=0.15)
        name = PICO_PIN_NAMES[pin].replace("(OUT)", "")
        if name == "ADC_VREF":
            name = "VREF"
        if name == "3V3_EN":
            name = "3V3EN"
        ty = hy + 0.95 if pin > 20 else hy - 0.65
        col = "#fff"
        if name == "GND":
            col = "#9fd3ff"
        if name in ("3V3", "VBUS"):
            col = "#ffb3b3"
        s.text(*P(hx, ty), name, size=0.9, anchor="middle", fill=col,
               weight="bold")
        s.text(*P(hx, ty + (0.75 if pin > 20 else -0.62)), str(pin),
               size=0.7, anchor="middle", fill="#b8e0c4")
    s.text(*P(20.6, 11.45), "A1  Raspberry Pi Pico 2", size=1.6,
           anchor="middle", fill="#fff", weight="bold")
    s.text(*P(20.6, 12.55), "USB end on the left · pin 1 = GP0 (row 6, col h)",
           size=0.95, anchor="middle", fill="#d0f0d8")


def draw_module(s, ref, p, front):
    x1, y1, x2, y2 = module_rect(ref)
    a = P(x1, y1)
    if not front:
        return
    s.rect(a[0], a[1], (x2 - x1) * U, (y2 - y1) * U, fill="#5b8bd6",
           stroke="#24467a", sw=0.35, rx=0.8,
           extra=' fill-opacity="0.10" stroke-dasharray="1.6 0.9"')
    for name, h in p["pins"].items():
        hx, hy = hole_xy(h)
        s.rect(*P(hx - 0.3, hy - 0.3), 0.6 * U, 0.6 * U, fill="#d4af37",
               stroke="#8a6d1a", sw=0.15)
        s.text(*P(hx, hy - 0.75), name, size=1.15, anchor="middle",
               fill="#13305e", weight="bold",
               extra=' paint-order="stroke" stroke="#fff" stroke-width="0.5"')
    cx = (x1 + x2) / 2
    if p["kind"] == "tft":
        lines = ["DS2  Adafruit 5394", "1.9\" ST7789 TFT 320x170 (SPI0)",
                 "body overhangs the top rails (outline approx.)",
                 "header order per Adafruit guide: check silkscreen"]
    else:
        lines = ["DS1  Adafruit 938", "1.3\" OLED 128x64, I2C 0x3C",
                 "CS, 3v3 not connected",
                 "body overhangs (outline approx.)"]
    for k, t in enumerate(lines):
        s.text(*P(cx, y1 + 1.7 + k * 1.2), t, size=2.1 if k == 0 else 1.6,
               anchor="middle", fill="#13305e",
               weight="bold" if k == 0 else "normal")


def draw_din(s, ref, front_ok=True):
    sk = DIN_SOCKETS[ref]
    cx, cy = sk["cx"], sk["cy"]
    w = 20.0 / 2.54
    front = cy + 2.5 / 2.54
    back = front - 15.8 / 2.54
    a = P(cx - w / 2, back)
    s.rect(a[0], a[1], w * U, (front - back) * U, fill="#3a3a3a",
           stroke="#111", sw=0.3, rx=0.4)
    # front plate
    f = P(cx - w / 2, front - 1.8 / 2.54)
    s.rect(f[0], f[1], w * U, (1.8 / 2.54) * U, fill="#555", stroke="none")
    for pin in ("1", "2", "3", "4", "5", "E1", "E2"):
        px, py = din_pin_xy(ref, pin)
        used = any(w_["a"] == f"{ref}.{pin}" or w_["b"] == f"{ref}.{pin}"
                   for w_ in WIRES)
        s.circle(*P(px, py), 0.32 * U, fill="#d9d9d9" if used else "#9a9a9a",
                 stroke="#000", sw=0.15)
        lab = pin if not pin.startswith("E") else "E"
        dy = 1.15 if not pin.startswith("E") else 1.1
        s.text(*P(px, py + dy), lab, size=1.3, anchor="middle", fill="#fff",
               weight="bold")
    # pegs
    for dx in (-7.5, 7.5):
        s.circle(*P(cx + dx / 2.54, cy - 5.0 / 2.54), 0.47 * U, fill="#222",
                 stroke="#777", sw=0.15)
    s.text(*P(cx, front + 1.25), f"{ref}  {sk['label']}", size=2.3,
           anchor="middle", weight="bold")
    s.text(*P(cx, front + 2.5),
           "SDS-50J top view, plug side facing down", size=1.6,
           anchor="middle", fill="#444")


def wire_svg(s, w):
    col = WIRE_COLOURS[w["col"]][0]
    pts = [P(x, y) for x, y in w["path"]]
    edge = "#000" if w["col"] != "gnd" else "#777"
    s.poly(pts, stroke=edge, sw=0.40 * U,
           extra=' stroke-linejoin="round" stroke-linecap="round" stroke-opacity="0.55"')
    s.poly(pts, stroke=col, sw=0.27 * U,
           extra=' stroke-linejoin="round" stroke-linecap="round"')
    for e, xy in ((w["a"], w["path"][0]), (w["b"], w["path"][-1])):
        c = P(*xy)
        s.circle(*c, 0.24 * U, fill=col, stroke="#000", sw=0.25)


def wire_label(s, w):
    # put the id on the longest segment
    best, bl = None, -1
    for a, b in zip(w["path"], w["path"][1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        if L > bl:
            bl, best = L, (a, b)
    (ax, ay), (bx, by) = best
    mx, my = (ax + bx) / 2, (ay + by) / 2
    if bl < 1.2:
        return
    x, y = P(mx, my)
    t = w["id"][1:]
    tw = 0.62 * len(t) + 0.3
    s.rect(x - tw / 2 * 1.0, y - 0.85, tw, 1.7, fill="#fff", stroke="#000",
           sw=0.12, rx=0.5)
    s.text(x, y + 0.55, t, size=1.45, anchor="middle", weight="bold")


LABELS = {  # ref -> (x, y, anchor) in u (absolute, sheet 1)
    "R1": (26.0, 46.3, "middle"), "R2": (30.2, 36.6, "start"),
    "R3": (12.4, 38.35, "start"), "R4": (7.3, 39.35, "end"),
    "R5": (61.0, 6.55, "middle"), "R6": (35.2, 36.6, "start"),
    "R7": (33.0, 49.6, "middle"), "R8": (53.2, 43.0, "start"),
    "R9": (49.2, 43.0, "start"), "C1": (26.9, 38.4, "end"),
    "C2": (57.9, 6.4, "end"), "D1": (13.9, 43.4, "end"),
    "D2": (44.1, 43.4, "start"), "D3": (28.6, 49.6, "start"),
    "Q1": (8.0, 43.3, "middle"), "SW1": (50.5, 13.7, "middle"),
}


def part_label_text(ref):
    p = PARTS[ref]
    v = p["value"].replace("R", "Ω") if p["value"].endswith("R") else p["value"]
    if ref.startswith("SW"):
        return f"{ref} 5-way nav"
    if ref == "U1":
        return "U1 H11L1M"
    return f"{ref} {v}"


def sheet1():
    s = Svg()
    frame(s, 1, "Layout (top view, milestone 1)")
    s.text(14, 14.5, "Sheet 1 · Breadboard layout, top view, drawn on a 0.1\" grid "
           f"(scale {U/2.54:.2f}:1)", size=4.2, weight="bold")
    for b in (1, 2):
        draw_board(s, b)
    # modules' header pins sit on board 1 (bodies drawn last, translucent)
    for ref, p in PARTS.items():
        if p["kind"] not in ("tft", "oled", "din"):
            draw_part(s, ref, p)
    for ref in DIN_SOCKETS:
        draw_din(s, ref)
    for w in WIRES:
        wire_svg(s, w)
    for ref in ("DS2", "DS1"):
        draw_module(s, ref, PARTS[ref], True)
    for w in WIRES:
        wire_label(s, w)
    for ref, (x, y, anc) in LABELS.items():
        s.text(*P(x, y), part_label_text(ref), size=1.6, anchor=anc,
               weight="bold", fill="#000",
               extra=' paint-order="stroke" stroke="#fff" stroke-width="0.7"')
    debug_probe_callout(s)
    usb_callout(s)
    legend(s)
    notes1(s)
    offboard_table(s)
    return s.render()


def debug_probe_callout(s):
    # probe box above board 1, left of the TFT
    bx, by, bw, bh = 4.0, -9.9, 22.5, 6.3
    a = P(bx, by)
    s.rect(a[0], a[1], bw * U, bh * U, fill="#f7f7f7", stroke="#c2185b",
           sw=0.4, rx=1)
    s.text(*P(bx + bw / 2, by + 1.5), "Raspberry Pi Debug Probe", size=2.4,
           anchor="middle", weight="bold")
    s.text(*P(bx + bw / 2, by + 2.75), "'D' (SWD) port \u2192 Pico 2 SWD header",
           size=1.7, anchor="middle")
    s.text(*P(bx + bw / 2, by + 3.9),
           "orange SC = SWCLK \u00b7 black GND \u00b7 yellow SD = SWDIO",
           size=1.6, anchor="middle")
    s.text(*P(bx + bw / 2, by + 5.05),
           "'U' port unused (UART0 = MIDI, log = USB CDC)", size=1.6,
           anchor="middle", fill="#555")
    # cable (drawn dashed, it sits above the board)
    for k, col in enumerate(("#ff8c00", "#111", "#e6c200")):
        yy = 11.5 - k
        pts = [P(26.4, yy), P(27.9, yy), P(27.9, -1.2 - k * 0.3),
               P(bx + bw - 1.5 - k * 0.6, -1.2 - k * 0.3),
               P(bx + bw - 1.5 - k * 0.6, by + bh)]
        s.poly(pts, stroke=col, sw=0.18 * U,
               extra=' stroke-dasharray="1.0 0.6" stroke-linejoin="round" '
                     'stroke-opacity="0.85"')
    s.text(*P(28.4, -0.2), "SWD cable (lies on top)", size=1.5, fill="#c2185b")


def usb_callout(s):
    s.text(*P(-2.6, -2.5), "USB to laptop", size=2.0, anchor="start",
           weight="bold", fill="#333")
    s.text(*P(-2.6, -1.3), "(5 V VBUS, UF2, CDC log)", size=1.6,
           anchor="start", fill="#333")
    pts = [P(-1.0, -0.8), P(-1.0, 6.0), P(5.9, 10.5)]
    s.poly(pts, stroke="#666", sw=0.3,
           extra=' stroke-dasharray="1 0.6" marker-end="url(#arr)"')


def offboard_table(s):
    x0, y0 = 255.0, 172.0
    s.text(x0, y0, "Off-board flying leads (DIN sockets)", size=3.6, weight="bold")
    y = y0 + 5.5
    xs = [x0, x0 + 13, x0 + 46, x0 + 84]
    for x, h in zip(xs, ["Wire", "Socket pin", "Plugs into", "Function"]):
        s.text(x, y, h, size=2.5, weight="bold")
    s.line(x0, y + 1.2, x0 + 152, y + 1.2, sw=0.3)
    y += 4.8
    for w in WIRES:
        if "." not in w["a"]:
            continue
        ref, pin = w["a"].split(".")
        col = WIRE_COLOURS[w["col"]][0]
        s.line(xs[0], y - 0.9, xs[0] + 5, y - 0.9, stroke=col, sw=1.2,
               extra=' stroke-linecap="round"')
        s.text(xs[0] + 6.5, y, w["id"][1:], size=2.4, weight="bold")
        s.text(xs[1], y, f"{ref} {DIN_SOCKETS[ref]['label'].split(' (')[0]} pin {pin}", size=2.4)
        s.text(xs[2], y, hole_label(w["b"]), size=2.4)
        fn = w["sig"].split(" to ", 1)[-1] if " to " in w["sig"] else w["sig"]
        s.text(xs[3], y, fn, size=2.4)
        y += 4.0
    s.text(x0, y + 1.0, "X1 MIDI IN: pins 1, 2, 3 and the shell tabs (E) stay "
           "unconnected. X2/X3: pins 1, 3 unconnected.", size=2.3, fill="#444")
    s.text(x0, y + 4.6, "Pin 2 is the shield pin. Front, top and solder-side "
           "views of the socket are on sheet 3.", size=2.3, fill="#444")


def legend(s):
    x0, y0 = 255.0, 22.0
    s.text(x0, y0, "Wire colours", size=3.6, weight="bold")
    y = y0 + 5
    for key, (col, lab) in WIRE_COLOURS.items():
        s.line(x0, y - 1.0, x0 + 10, y - 1.0, stroke="#000", sw=0.40 * U,
               extra=' stroke-linecap="round" stroke-opacity="0.55"')
        s.line(x0, y - 1.0, x0 + 10, y - 1.0, stroke=col, sw=0.27 * U,
               extra=' stroke-linecap="round"')
        s.text(x0 + 13, y, lab, size=2.6)
        y += 4.3
    y += 1
    s.text(x0, y, "Symbols", size=3.6, weight="bold")
    y += 5
    s.rect(x0, y - 2.6, 4, 3, fill="#5b8bd6", stroke="#24467a", sw=0.35,
           extra=' fill-opacity="0.10" stroke-dasharray="1.6 0.9"')
    s.text(x0 + 13, y, "Module body on its header (wires may pass under it)",
           size=2.4)
    y += 4.3
    s.circle(x0 + 5, y - 1.0, 0.24 * U, fill="#d62728", stroke="#000", sw=0.25)
    s.text(x0 + 13, y, "Wire end: plug into this hole. 12 = wire W12 in the netlist",
           size=2.4)
    y += 4.3
    s.text(x0 + 13, y, "Rails: outer = GND (blue), inner = supply (red)",
           size=2.4)
    y += 3.6
    s.text(x0 + 13, y, "B1 inner rails 3V3 · B2 top inner 3V3, bottom inner 5V",
           size=2.4)


def notes1(s):
    x0, y0 = 255.0, 98.0
    s.text(x0, y0, "Read me first", size=3.6, weight="bold")
    notes = [
        "Board 1 = MCU + UI.  Board 2 = MIDI IN, relays, OUT A/B drivers.",
        "Pico 2: USB end off the left edge of B1, pin 1 (GP0) at h6,",
        "  pin 40 (VBUS) at c6.  Free holes per Pico pin: a/b and i/j.",
        "B2 bottom inner rail is 5 V (VBUS) - relay coils only.",
        "  Everything else runs from 3V3 (Pico pin 36).",
        "Check rail continuity at the middle of each rail with a meter:",
        "  some full-size boards split the rails between rows 31 and 33.",
        "Resistors lie flat over the holes they span (unused holes).",
        "D3 stands upright between j26 (cathode, band) and j27.",
        "Relay pin numbers are the Omron G5V-2 terminal numbers;",
        "  RL2 is rotated 180° (coil end on the right).",
        "DIN sockets: short flying leads soldered to the socket pins.",
        "  IN shell (E) and pins 1, 2, 3 stay unconnected.",
        "OLED DC is tied to GND to force I2C address 0x3C (see sheet 3).",
        "Both displays can stay fitted: firmware uses the OLED if it answers",
        "  at 0x3C, else the TFT.  To test the TFT, pull W25 (OLED Vin).",
        "Joystick: as drawn, push towards board-1 row a = UP.",
        "  Directions follow the ALPS SKQUCA pinout - verify with a meter.",
        "Every connection was machine-checked against docs/wiring.md.",
    ]
    y = y0 + 5
    for n in notes:
        s.text(x0, y, n, size=2.45)
        y += 3.55


# ----------------------------------------------------------------------------
def build_pdf(svgs, out):
    from playwright.sync_api import sync_playwright
    pages = "".join(f'<div class="page">{svg[svg.index("<svg"):]}</div>'
                    for svg in svgs)
    doc = ("<!doctype html><html><head><meta charset='utf-8'><style>"
           "@page { size: 420mm 297mm; margin: 0 }"
           "html,body { margin:0; padding:0 }"
           ".page { width:420mm; height:297mm; page-break-after: always;"
           " overflow:hidden }"
           ".page:last-child { page-break-after: auto }"
           ".page svg { display:block; width:420mm; height:297mm }"
           "</style></head><body>" + pages + "</body></html>")
    tmp = os.path.join(HERE, ".pdf_build.html")
    with open(tmp, "w") as f:
        f.write(doc)
    try:
        with sync_playwright() as p:
            b = p.chromium.launch()
            pg = b.new_page()
            pg.goto("file://" + tmp)
            pg.wait_for_timeout(300)
            pg.pdf(path=out, width="420mm", height="297mm",
                   print_background=True, prefer_css_page_size=True)
            b.close()
    finally:
        os.remove(tmp)


def main():
    errs, _ = bb_route.check_nets()
    if errs:
        print("NETLIST ERRORS:")
        for e in errs:
            print("  ", e)
        sys.exit(1)
    print(f"netlist OK: {len(EXPECTED_NETS)} nets from wiring.md match the "
          f"breadboard ({len(WIRES)} wires, {len(PARTS)} parts)")
    failed = bb_route.route_all()
    if failed:
        print("ROUTING FAILED:", failed)
        sys.exit(1)
    import bb_sheets
    bb_sheets.Svg = Svg
    bb_sheets.frame = frame
    defs = ('<defs><marker id="arr" viewBox="0 0 6 6" refX="5" refY="3" '
            'markerWidth="4" markerHeight="4" orient="auto">'
            '<path d="M0,0 L6,3 L0,6 z" fill="#666"/></marker></defs>')
    pages = [sheet1(), bb_sheets.sheet2(), bb_sheets.sheet3(),
             bb_sheets.sheet4()]
    pages = [p.replace('<rect x="0" y="0"', defs + '<rect x="0" y="0"', 1)
             for p in pages]
    names = ["MIDIum_breadboard_p1_layout.svg",
             "MIDIum_breadboard_p2_netlist.svg",
             "MIDIum_breadboard_p3_pinouts.svg",
             "MIDIum_breadboard_p4_build.svg"]
    for n, svg in zip(names, pages):
        with open(os.path.join(HERE, n), "w") as f:
            f.write(svg)
    print("wrote", ", ".join(names))
    if "--no-pdf" not in sys.argv:
        out = os.path.join(HERE, "MIDIum_breadboard_layout.pdf")
        build_pdf(pages, out)
        print("wrote", os.path.basename(out))


if __name__ == "__main__":
    main()
