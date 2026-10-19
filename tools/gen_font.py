#!/usr/bin/env python3
"""
gen_font.py - rasterise JetBrains Mono into MIDIum's anti-aliased TFT font.

Writes src/font_jbm13.h: 8 x 15 px cells, 4-bit coverage (16 alpha levels),
two pixels per byte (high nibble = left pixel), rows top to bottom, 60 bytes
per glyph. The TFT driver alpha-blends each cell between its background and
foreground colours (src/disp_tft.c).

  python3 tools/gen_font.py                      # regenerate src/font_jbm13.h
  python3 tools/gen_font.py --preview out.png    # also write a zoomed sheet

Needs Python 3 + Pillow (built with FreeType). The font is
tools/fonts/JetBrainsMono-Medium.ttf, JetBrains Mono v2.305 Medium from
https://github.com/JetBrains/JetBrainsMono (fonts/ttf), SIL Open Font
Licence 1.1 (tools/fonts/OFL.txt, THIRD_PARTY.md).

Glyph set:
  0x20..0x7E  printable ASCII, rendered by FreeType (with the font's hinting)
              at 13 px; baseline on cell row 12 (rows 0-11 above it, 12-14
              below), so descenders (g j p q y _ , ;) keep three rows. The
              few glyphs taller than the cell ($ [ ] { }, 1 px over the top)
              are moved down 1 px rather than clipped.
  0x10..0x15  battery levels 0-4 and the charging bolt (hand drawn, see
              ICONS) - same codes as src/glyphs.h for the OLED
  0x16, 0x17  play right / left triangles (MTC direction)
  0x18..0x1B  arrows up, down, right, left (from the font: U+2191 U+2193
              U+2192 U+2190; CP437 positions, as in src/font5x7.h)
  0x1C, 0x1D  progress bar: full cell, empty track (TFT only)
Anything else draws as '?'.

The Reserved Font Name rule of the OFL applies to modified versions of the
font software. Rasterising glyphs into a bitmap table for a device is
embedding, not modification; the table keeps the name and copyright.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

CELL_W, CELL_H = 8, 15
BASELINE = 12          # first cell row below the baseline
SIZE_PX = 13

# Hand-drawn icons, 8 x 15, one hex digit (alpha 0-F) per pixel, '.' = 0.
def battery(level):
    rows = ["........",
            "........",
            "..FFFF..",     # terminal
            ".FFFFFF.",     # top
            ]
    inner = 8                                   # rows 4..11
    for r in range(inner):
        filled = r >= inner - 2 * level
        rows.append(".FFFFFF." if filled else ".F....F.")
    rows += [".FFFFFF.",                        # bottom (row 12)
             "........",
             "........"]
    return rows

BOLT = ["........",
        "........",
        ".....8F.",
        "....8FB.",
        "...8FB..",
        "..8FB...",
        ".BFFFFF.",
        ".8BBFFB.",
        "....FB..",
        "...BF8..",
        "..BF8...",
        ".BF8....",
        ".F8.....",
        "........",
        "........"]

PLAY_R = ["........",
          "........",
          "........",
          ".F8.....",
          ".FFB4...",
          ".FFFFA3.",
          ".FFFFFF8",
          ".FFFFFFF",
          ".FFFFFF8",
          ".FFFFA3.",
          ".FFB4...",
          ".F8.....",
          "........",
          "........",
          "........"]
PLAY_L = [r[::-1] for r in PLAY_R]

# progress bar cells: full (rows 3-11) and the empty track (same, faint)
BAR_FULL = ["........"] * 3 + ["FFFFFFFF"] * 9 + ["........"] * 3
BAR_EMPTY = ["........"] * 3 + ["44444444"] * 9 + ["........"] * 3

ICONS = {
    0x10: battery(0), 0x11: battery(1), 0x12: battery(2), 0x13: battery(3), 0x14: battery(4),
    0x15: BOLT, 0x16: PLAY_R, 0x17: PLAY_L, 0x1C: BAR_FULL, 0x1D: BAR_EMPTY,
}
FROM_FONT = {0x18: 0x2191, 0x19: 0x2193, 0x1A: 0x2192, 0x1B: 0x2190}

FIRST_EXTRA, LAST_EXTRA = 0x10, 0x1D
FIRST_ASCII, LAST_ASCII = 0x20, 0x7E


def render(font, cp, gamma):
    """Coverage 0..15 for code point cp, fitted into the cell."""
    W, H, OX, OY = 32, 40, 8, 24
    im = Image.new("L", (W, H), 0)
    ImageDraw.Draw(im).text((OX, OY), chr(cp), fill=255, font=font, anchor="ls")
    bb = im.getbbox()
    cell = [[0] * CELL_W for _ in range(CELL_H)]
    if not bb:
        return cell, (0, 0)
    x0, y0, x1, y1 = bb[0] - OX, bb[1] - (OY - BASELINE), bb[2] - OX, bb[3] - (OY - BASELINE)
    dx = dy = 0
    if x0 < 0:
        dx = -x0
    elif x1 > CELL_W:
        dx = CELL_W - x1
    if y0 < 0:
        dy = min(-y0, CELL_H - y1)
    elif y1 > CELL_H:
        dy = CELL_H - y1
    for y in range(CELL_H):
        for x in range(CELL_W):
            sx, sy = x - dx + OX, y - dy + (OY - BASELINE)
            a = im.getpixel((sx, sy)) if 0 <= sx < W and 0 <= sy < H else 0
            if a:
                a = (a / 255.0) ** gamma
                cell[y][x] = int(round(a * 15))
    # anything the shift could not bring inside is clipped: report it
    lost = 0
    for y in range(H):
        for x in range(W):
            cx, cy = x - OX + dx, y - (OY - BASELINE) + dy
            if im.getpixel((x, y)) and not (0 <= cx < CELL_W and 0 <= cy < CELL_H):
                lost += 1
    return cell, (dy, lost)


def icon(rows):
    assert len(rows) == CELL_H and all(len(r) == CELL_W for r in rows), rows
    return [[0 if c == "." else int(c, 16) for c in r] for r in rows]


def pack(cell):
    out = []
    for row in cell:
        for x in range(0, CELL_W, 2):
            out.append((row[x] << 4) | row[x + 1])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ttf", default=os.path.join(HERE, "fonts", "JetBrainsMono-Medium.ttf"))
    ap.add_argument("--out", default=os.path.join(ROOT, "src", "font_jbm13.h"))
    ap.add_argument("--gamma", type=float, default=0.85,
                    help="coverage exponent (<1 thickens slightly: light text on a dark panel)")
    ap.add_argument("--preview", help="write a zoomed PNG sheet of every glyph")
    a = ap.parse_args()

    font = ImageFont.truetype(a.ttf, SIZE_PX)
    glyphs = {}
    notes = []
    clipped = 0
    for cp in range(FIRST_ASCII, LAST_ASCII + 1):
        cell, (dy, lost) = render(font, cp, a.gamma)
        glyphs[cp] = cell
        clipped += lost
        if dy or lost:
            notes.append(f"{chr(cp)!r}: moved {dy:+d} px" + (f", {lost} px clipped" if lost else ""))
    for cp in range(FIRST_EXTRA, LAST_EXTRA + 1):
        if cp in ICONS:
            glyphs[cp] = icon(ICONS[cp])
        else:
            cell, (dy, lost) = render(font, FROM_FONT[cp], a.gamma)
            glyphs[cp] = cell
            clipped += lost
            if lost:
                notes.append(f"U+{FROM_FONT[cp]:04X}: {lost} px clipped")

    order = list(range(FIRST_EXTRA, LAST_EXTRA + 1)) + list(range(FIRST_ASCII, LAST_ASCII + 1))
    lines = []
    lines.append("/*")
    lines.append(" * font_jbm13.h - JetBrains Mono Medium, 13 px, anti-aliased (4-bit) TFT font.")
    lines.append(" * GENERATED by tools/gen_font.py - do not edit; rerun the script instead.")
    lines.append(" *")
    lines.append(" * Font: JetBrains Mono (Medium), Copyright 2020 The JetBrains Mono Project")
    lines.append(" * Authors (https://github.com/JetBrains/JetBrainsMono). Licensed under the")
    lines.append(" * SIL Open Font License, Version 1.1 (THIRD_PARTY.md, tools/fonts/OFL.txt).")
    lines.append(" * Rasterised glyph bitmaps of the unmodified font; the battery, bolt, play")
    lines.append(" * and bar icons (0x10-0x17, 0x1C, 0x1D) are MIDIum's own.")
    lines.append(" *")
    lines.append(f" * Cell {CELL_W} x {CELL_H} px, baseline under row {BASELINE - 1}. Each glyph: {CELL_H} rows of")
    lines.append(f" * {CELL_W // 2} bytes, two pixels per byte (high nibble = left), alpha 0..15.")
    lines.append(f" * Coverage gamma {a.gamma}.")
    for n in notes:
        lines.append(f" *   {n}")
    lines.append(" */")
    lines.append("#ifndef MIDIUM_FONT_JBM13_H")
    lines.append("#define MIDIUM_FONT_JBM13_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define JBM13_W        {CELL_W}")
    lines.append(f"#define JBM13_H        {CELL_H}")
    lines.append(f"#define JBM13_BASELINE {BASELINE}")
    lines.append(f"#define JBM13_BYTES    {CELL_W * CELL_H // 2}")
    lines.append(f"#define JBM13_FIRST_EXTRA 0x{FIRST_EXTRA:02X}u")
    lines.append(f"#define JBM13_LAST_EXTRA  0x{LAST_EXTRA:02X}u")
    lines.append(f"#define JBM13_FIRST_ASCII 0x{FIRST_ASCII:02X}u")
    lines.append(f"#define JBM13_LAST_ASCII  0x{LAST_ASCII:02X}u")
    lines.append(f"#define JBM13_COUNT    {len(order)}u")
    lines.append(f"#define JBM13_CLIPPED_PX {clipped}   /* font pixels that did not fit the cell */")
    lines.append("")
    lines.append(f"static const uint8_t font_jbm13[JBM13_COUNT][JBM13_BYTES] = {{")
    for cp in order:
        b = pack(glyphs[cp])
        name = repr(chr(cp)) if cp >= 0x20 else f"icon 0x{cp:02X}"
        name = name.replace("*/", "* /").replace("\\", "backslash")
        lines.append(f"    {{ /* 0x{cp:02X} {name} */")
        for r in range(CELL_H):
            row = b[r * 4:(r + 1) * 4]
            art = "".join(".123456789ABCDEF"[v] for v in glyphs[cp][r])
            lines.append("        " + ", ".join(f"0x{v:02X}" for v in row) + f",  /* {art} */")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("/* Glyph for a character code: ASCII, the extra icons, else '?'. */")
    lines.append("static inline const uint8_t *jbm13_glyph(uint8_t ch) {")
    lines.append("    if (ch >= JBM13_FIRST_ASCII && ch <= JBM13_LAST_ASCII)")
    lines.append("        return font_jbm13[(JBM13_LAST_EXTRA - JBM13_FIRST_EXTRA + 1u) + (ch - JBM13_FIRST_ASCII)];")
    lines.append("    if (ch >= JBM13_FIRST_EXTRA && ch <= JBM13_LAST_EXTRA)")
    lines.append("        return font_jbm13[ch - JBM13_FIRST_EXTRA];")
    lines.append("    return jbm13_glyph('?');")
    lines.append("}")
    lines.append("")
    lines.append("#endif /* MIDIUM_FONT_JBM13_H */")
    with open(a.out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {a.out}: {len(order)} glyphs, {len(order) * CELL_W * CELL_H // 2} bytes")
    for n in notes:
        print("  note:", n)

    if a.preview:
        Z, COLS = 6, 16
        rows = (len(order) + COLS - 1) // COLS
        sheet = Image.new("RGB", (COLS * (CELL_W + 2) * Z, rows * (CELL_H + 2) * Z), (40, 0, 0))
        for i, cp in enumerate(order):
            gx, gy = (i % COLS) * (CELL_W + 2) * Z, (i // COLS) * (CELL_H + 2) * Z
            for y in range(CELL_H):
                for x in range(CELL_W):
                    v = glyphs[cp][y][x] * 17
                    col = (v, v, v) if y != BASELINE else (v, v, max(v, 60))
                    for yy in range(Z):
                        for xx in range(Z):
                            sheet.putpixel((gx + x * Z + xx, gy + y * Z + yy), col)
        sheet.save(a.preview)
        print("preview:", a.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
