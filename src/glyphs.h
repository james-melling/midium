/*
 * glyphs.h - MIDIum's own 5x7 icon glyphs for the OLED, drawn in place of the
 * font for a few control codes that never occur in text (display.h CH_BATT*,
 * CH_BOLT, CH_PLAY_*; the arrows CH_UP.. are the font's own CP437 glyphs).
 * The TFT font (font_jbm13.h) has the same codes.
 * Same format as font5x7.h: 5 columns, LSB = top pixel.
 *
 *   battery (upright, terminal on top), fill levels 0..4 from the bottom:
 *     .###.      bit 0  terminal
 *     #####      bit 1  top
 *     #...#      bits 2-5: fill rows (level 4 = all four)
 *     #...#
 *     #...#
 *     #...#
 *     #####      bit 6  bottom
 *   bolt (charging):
 *     ...##
 *     ..##.
 *     .##..
 *     #####
 *     ..##.
 *     .##..
 *     ##...
 *   play right / left: solid triangles 7 high.
 *   routing matrix cells (0x1E, 0x1F; display.h CH_DOT_*): a 5x5 disc
 *   (routed) and a single centre pixel (not routed), in place of the
 *   font's CP437 up / down triangles there.
 */
#ifndef MIDIUM_GLYPHS_H
#define MIDIUM_GLYPHS_H

#include <stdint.h>
#include "font5x7.h"

#define GLYPH_FIRST 0x10u
#define GLYPH_COUNT 8u

static const uint8_t glyphs5x7[GLYPH_COUNT * 5] = {
    0x7E, 0x43, 0x43, 0x43, 0x7E,   /* 0x10 battery empty */
    0x7E, 0x63, 0x63, 0x63, 0x7E,   /* 0x11 battery 1/4 */
    0x7E, 0x73, 0x73, 0x73, 0x7E,   /* 0x12 battery 2/4 */
    0x7E, 0x7B, 0x7B, 0x7B, 0x7E,   /* 0x13 battery 3/4 */
    0x7E, 0x7F, 0x7F, 0x7F, 0x7E,   /* 0x14 battery full */
    0x48, 0x6C, 0x3E, 0x1B, 0x09,   /* 0x15 charging bolt */
    0x7F, 0x3E, 0x1C, 0x08, 0x00,   /* 0x16 play right */
    0x00, 0x08, 0x1C, 0x3E, 0x7F,   /* 0x17 play left */
};

#define GLYPH_DOT_FIRST 0x1Eu
static const uint8_t glyphs5x7_dot[2 * 5] = {
    0x1C, 0x3E, 0x3E, 0x3E, 0x1C,   /* 0x1E routed: disc */
    0x00, 0x00, 0x08, 0x00, 0x00,   /* 0x1F not routed: dot */
};

static inline const uint8_t *glyph5x7(uint8_t ch) {
    if (ch >= GLYPH_FIRST && ch < GLYPH_FIRST + GLYPH_COUNT) return &glyphs5x7[(ch - GLYPH_FIRST) * 5u];
    if (ch == GLYPH_DOT_FIRST || ch == GLYPH_DOT_FIRST + 1u) return &glyphs5x7_dot[(ch - GLYPH_DOT_FIRST) * 5u];
    return &font5x7[ch * 5u];
}

#endif /* MIDIUM_GLYPHS_H */
