/*
 * tft_render.h - portable half of the ST7789 driver: the colour palette,
 * alpha blending of the anti-aliased font, and rasterising a span of text
 * cells into a line buffer. No hardware access (unit tested on the host);
 * src/disp_tft.c sends the result over SPI.
 */
#ifndef MIDIUM_TFT_RENDER_H
#define MIDIUM_TFT_RENDER_H

#include <stdint.h>
#include <stdbool.h>
#include "display.h"

#define TFT_CELL_W 8
#define TFT_CELL_H 15

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define TFT_BG_COLOUR      RGB565(0x00, 0x00, 0x00)
#define TFT_INV_BG_COLOUR  RGB565(0x1C, 0x48, 0x98)   /* highlighted row / header bar */
#define TFT_ERR_BG_COLOUR  RGB565(0xA8, 0x1C, 0x1C)   /* inverse error rows: power off, delete */
#define TFT_CUR_BG_COLOUR  RGB565(0x30, 0x36, 0x40)   /* cursor row (pinned monitor) */

/* Foreground for a colour class (msg_class_t). */
uint16_t tft_class_colour(uint8_t cls);

/* Foreground and background for a cell attribute. */
void tft_attr_colours(uint8_t attr, uint16_t *fg, uint16_t *bg);

/* fg over bg at coverage a (0..15) per RGB565 channel; a = 0 gives bg,
 * a = 15 gives fg exactly. */
uint16_t tft_blend565(uint16_t fg, uint16_t bg, unsigned a);

/* The 16-entry ramp tft_blend565(fg, bg, 0..15). */
void tft_ramp(uint16_t fg, uint16_t bg, uint16_t ramp[16]);

/* Rasterise cells[c0..c1) into out: (c1 - c0) * TFT_CELL_W pixels per line,
 * TFT_CELL_H lines, RGB565 in big-endian byte order (ready for RAMWR).
 * The blend ramp of the last fg / bg pair is cached. */
void tft_render_cells(const cell_t *cells, int c0, int c1, uint16_t *out);

/* Columns [*c0, *c1) where row `now` differs from `prev`. False if none. */
bool tft_row_span(const cell_t *now, const cell_t *prev, int cols, int *c0, int *c1);

/* Ramp cache statistics (tests): number of ramps computed so far. */
uint32_t tft_ramp_builds(void);

#endif /* MIDIUM_TFT_RENDER_H */
