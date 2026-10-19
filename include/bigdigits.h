/*
 * bigdigits.h - seven-segment style large characters drawn into a 1-bit
 * bitmap (MSB first, rows of `stride` bytes), for the full-screen MTC
 * display. Portable C. Any size: segment thickness t, cell w x h pixels.
 * Characters: 0-9, ':' ';' '-' ' '.
 */
#ifndef MIDIUM_BIGDIGITS_H
#define MIDIUM_BIGDIGITS_H

#include <stdint.h>

/* Clears the w x h cell at (x, y) and draws c in it. */
void bd_glyph(uint8_t *bits, int stride, int x, int y, int w, int h, int t, char c);

static inline void bd_set(uint8_t *bits, int stride, int x, int y) {
    bits[y * stride + (x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
}
static inline int bd_get(const uint8_t *bits, int stride, int x, int y) {
    return (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
}

#endif
