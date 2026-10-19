/*
 * tft_render.c - palette, alpha blending and text rasterising for the TFT.
 * See tft_render.h. Portable C.
 */
#include "tft_render.h"
#include "midi_decode.h"
#include "font_jbm13.h"

#if JBM13_W != TFT_CELL_W || JBM13_H != TFT_CELL_H
#error "font_jbm13.h cell size does not match the TFT cell"
#endif

/* Class colours on black. Checked with the anti-aliased font: the
 * mid-tones (grey, blue-ish cyan) are kept light enough that the partial
 * pixels of 1-px stems stay readable. */
static const uint16_t s_fg[CLS_COUNT] = {
    [CLS_NORMAL]   = RGB565(0xFF, 0xFF, 0xFF),
    [CLS_NOTE]     = RGB565(0x50, 0xF0, 0x50),
    [CLS_CC]       = RGB565(0x50, 0xD8, 0xFF),
    [CLS_PC]       = RGB565(0xFF, 0xD8, 0x40),
    [CLS_SYSEX]    = RGB565(0xFF, 0x68, 0xFF),
    [CLS_REALTIME] = RGB565(0xA0, 0xA0, 0xA8),
    [CLS_ERROR]    = RGB565(0xFF, 0x50, 0x50),
    [CLS_HEADER]   = RGB565(0xFF, 0xB0, 0x30),
    [CLS_DIM]      = RGB565(0x90, 0x94, 0x9C),
};

uint16_t tft_class_colour(uint8_t cls) { return s_fg[cls < CLS_COUNT ? cls : 0]; }

void tft_attr_colours(uint8_t attr, uint16_t *fg, uint16_t *bg) {
    uint8_t cls = ATTR_CLASS(attr);
    *fg = tft_class_colour(cls);
    *bg = TFT_BG_COLOUR;
    if (attr & ATTR_INVERSE) {
        *bg = TFT_INV_BG_COLOUR;
        if (cls == CLS_NORMAL) *fg = 0xFFFFu;
        /* red on blue is hard to read: errors / warnings get a red bar */
        if (cls == CLS_ERROR) { *bg = TFT_ERR_BG_COLOUR; *fg = 0xFFFFu; }
    } else if (attr & ATTR_CURSOR) {
        *bg = TFT_CUR_BG_COLOUR;
    }
}

static inline unsigned mix(unsigned f, unsigned b, unsigned a) {
    /* b + (f - b) * a / 15, rounded, without signed arithmetic */
    return f >= b ? b + ((f - b) * a + 7u) / 15u : b - ((b - f) * a + 7u) / 15u;
}

uint16_t tft_blend565(uint16_t fg, uint16_t bg, unsigned a) {
    if (a >= 15u) return fg;
    if (a == 0u) return bg;
    unsigned r = mix(fg >> 11, bg >> 11, a);
    unsigned g = mix((fg >> 5) & 0x3Fu, (bg >> 5) & 0x3Fu, a);
    unsigned b = mix(fg & 0x1Fu, bg & 0x1Fu, a);
    return (uint16_t)(r << 11 | g << 5 | b);
}

void tft_ramp(uint16_t fg, uint16_t bg, uint16_t ramp[16]) {
    for (unsigned a = 0; a < 16u; a++) ramp[a] = tft_blend565(fg, bg, a);
}

static uint16_t s_rfg, s_rbg, s_ramp_be[16];
static bool     s_rvalid;
static uint32_t s_rbuilds;

uint32_t tft_ramp_builds(void) { return s_rbuilds; }

static const uint16_t *ramp_for(uint16_t fg, uint16_t bg) {
    if (!s_rvalid || fg != s_rfg || bg != s_rbg) {
        uint16_t r[16];
        tft_ramp(fg, bg, r);
        for (int i = 0; i < 16; i++) s_ramp_be[i] = (uint16_t)((r[i] >> 8) | (r[i] << 8));
        s_rfg = fg;
        s_rbg = bg;
        s_rvalid = true;
        s_rbuilds++;
    }
    return s_ramp_be;
}

void tft_render_cells(const cell_t *cells, int c0, int c1, uint16_t *out) {
    const int w = (c1 - c0) * TFT_CELL_W;
    for (int c = c0; c < c1; c++) {
        uint8_t attr = cells[c].attr;
        uint16_t fg, bg;
        tft_attr_colours(attr, &fg, &bg);
        const uint16_t *ramp = ramp_for(fg, bg);
        const uint8_t *g = jbm13_glyph((uint8_t)cells[c].ch);
        uint16_t *p = out + (c - c0) * TFT_CELL_W;
        for (int y = 0; y < TFT_CELL_H; y++, p += w, g += TFT_CELL_W / 2) {
            for (int x = 0; x < TFT_CELL_W / 2; x++) {
                p[2 * x]     = ramp[g[x] >> 4];
                p[2 * x + 1] = ramp[g[x] & 0x0Fu];
            }
        }
        if (attr & ATTR_CURSOR) {                 /* underline on the bottom line */
            uint16_t *u = out + (size_t)(TFT_CELL_H - 1) * (size_t)w + (c - c0) * TFT_CELL_W;
            for (int x = 0; x < TFT_CELL_W; x++) u[x] = ramp[15];
        }
    }
}

bool tft_row_span(const cell_t *now, const cell_t *prev, int cols, int *c0, int *c1) {
    int a = 0, b = cols;
    while (a < cols && now[a].ch == prev[a].ch && now[a].attr == prev[a].attr) a++;
    if (a == cols) return false;
    while (b > a && now[b - 1].ch == prev[b - 1].ch && now[b - 1].attr == prev[b - 1].attr) b--;
    *c0 = a;
    *c1 = b;
    return true;
}
