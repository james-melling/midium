/*
 * bigdigits.c - seven-segment glyphs. See bigdigits.h.
 *
 *    aaa
 *   f   b      Segments are drawn with pointed ends (each row / column of
 *   f   b      a segment is inset by its distance from the segment's centre
 *    ggg       line), with a one-pixel gap where segments meet.
 *   e   c
 *   e   c
 *    ddd
 */
#include "bigdigits.h"

static void hseg(uint8_t *b, int st, int x0, int x1, int yc, int t) {
    int half = t / 2;
    for (int dy = -half; dy < t - half; dy++) {
        int in = dy < 0 ? -dy : dy;
        for (int x = x0 + in; x <= x1 - in; x++) bd_set(b, st, x, yc + dy);
    }
}

static void vseg(uint8_t *b, int st, int xc, int y0, int y1, int t) {
    int half = t / 2;
    for (int dx = -half; dx < t - half; dx++) {
        int in = dx < 0 ? -dx : dx;
        for (int y = y0 + in; y <= y1 - in; y++) bd_set(b, st, xc + dx, y);
    }
}

static void box(uint8_t *b, int st, int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) bd_set(b, st, x, y);
}

void bd_glyph(uint8_t *bits, int stride, int x, int y, int w, int h, int t, char c) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) bits[yy * stride + (xx >> 3)] &= (uint8_t)~(0x80u >> (xx & 7));
    if (t < 1) t = 1;
    int half = t / 2;
    int xl = x + half, xr = x + w - 1 - (t - 1 - half);       /* vertical segment centre lines */
    int yt = y + half, yb = y + h - 1 - (t - 1 - half);       /* horizontal segment centre lines */
    int ym = y + (h - 1) / 2;
    int g = 1 + t / 4;                                        /* gap at the joints */
    static const uint8_t SEG[10] = {
        /* gfedcba */
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    };
    uint8_t s = 0;
    if (c >= '0' && c <= '9') s = SEG[c - '0'];
    else if (c == '-') s = 0x40;
    else if (c == ':' || c == ';') {
        int d = t + t / 4;
        if (d > w) d = w;
        int dx = x + (w - d) / 2;
        box(bits, stride, dx, y + h * 3 / 10 - d / 2, d, d);
        box(bits, stride, dx, y + h * 7 / 10 - d / 2, d, d);
        if (c == ';') {                                       /* comma tail below the lower dot */
            int ty = y + h * 7 / 10 + (d + 1) / 2;
            for (int k = 0; k < d && ty + k < y + h; k++) {
                int tw = d / 2 > 0 ? d / 2 : 1;
                int tx = dx + d - tw - k / 2;
                if (tx < x) tx = x;
                box(bits, stride, tx, ty + k, tw, 1);
            }
        }
        return;
    }
    if (s & 0x01) hseg(bits, stride, xl + g, xr - g, yt, t);          /* a */
    if (s & 0x02) vseg(bits, stride, xr, yt + g, ym - g, t);          /* b */
    if (s & 0x04) vseg(bits, stride, xr, ym + g, yb - g, t);          /* c */
    if (s & 0x08) hseg(bits, stride, xl + g, xr - g, yb, t);          /* d */
    if (s & 0x10) vseg(bits, stride, xl, ym + g, yb - g, t);          /* e */
    if (s & 0x20) vseg(bits, stride, xl, yt + g, ym - g, t);          /* f */
    if (s & 0x40) hseg(bits, stride, xl + g, xr - g, ym, t);          /* g */
}
