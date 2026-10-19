/*
 * v0.6 tests: the anti-aliased TFT font table, alpha blending and row
 * rasterising, the console at every grid size, decode widths at the new
 * monitor widths, the MTC source selection (two inputs) and the monitor's
 * freeze-on-pause history / scroll logic.
 */
#include <stdlib.h>
#include "test_common.h"
#include "display.h"
#include "tft_render.h"
#include "font_jbm13.h"
#include "glyphs.h"
#include "midi_decode.h"
#include "msc_decode.h"
#include "mtc.h"
#include "mon_view.h"

/* ------------------------------------------------------------------------ */
/* font                                                                      */
/* ------------------------------------------------------------------------ */
static int px(const uint8_t *g, int x, int y) {
    uint8_t b = g[y * (JBM13_W / 2) + x / 2];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}
static int ink_rows(const uint8_t *g, int *top, int *bot) {
    int n = 0;
    *top = 99; *bot = -1;
    for (int y = 0; y < JBM13_H; y++) {
        bool any = false;
        for (int x = 0; x < JBM13_W; x++) if (px(g, x, y)) any = true;
        if (any) { n++; if (y < *top) *top = y; *bot = y; }
    }
    return n;
}
static int ink_total(const uint8_t *g) {
    int s = 0;
    for (int y = 0; y < JBM13_H; y++) for (int x = 0; x < JBM13_W; x++) s += px(g, x, y);
    return s;
}

static void test_font(void) {
    printf("font_jbm13\n");
    CHECK(JBM13_W == 8 && JBM13_H == 15 && JBM13_BYTES == 60);
    CHECK(JBM13_CLIPPED_PX == 0);                       /* every glyph fits the 8 x 15 cell */
    CHECK(JBM13_COUNT == (JBM13_LAST_EXTRA - JBM13_FIRST_EXTRA + 1u) + (JBM13_LAST_ASCII - JBM13_FIRST_ASCII + 1u));
    /* every printable glyph present (has ink), space blank */
    int missing = 0;
    for (int c = 0x21; c <= 0x7E; c++) if (ink_total(jbm13_glyph((uint8_t)c)) == 0) missing++;
    CHECK(missing == 0);
    CHECK(ink_total(jbm13_glyph(' ')) == 0);
    /* distinct glyphs (no two printable characters share a bitmap) */
    int dup = 0;
    for (int a = 0x21; a <= 0x7E; a++)
        for (int b = a + 1; b <= 0x7E; b++)
            if (!memcmp(jbm13_glyph((uint8_t)a), jbm13_glyph((uint8_t)b), JBM13_BYTES)) dup++;
    CHECK(dup == 0);
    /* unknown codes fall back to '?' */
    CHECK(jbm13_glyph(0x80) == jbm13_glyph('?'));
    CHECK(jbm13_glyph(0x00) == jbm13_glyph('?'));
    CHECK(jbm13_glyph(0x7F) == jbm13_glyph('?'));
    /* baseline: capitals and digits sit on row 11, nothing below */
    const char *base = "HEIMNTXZ0123456789abcdehkmnorstuvwxz";
    int t, b, bad = 0;
    for (const char *p = base; *p; p++) {
        ink_rows(jbm13_glyph((uint8_t)*p), &t, &b);
        if (b != JBM13_BASELINE - 1) bad++;
    }
    CHECK(bad == 0);
    /* cap height: H spans rows 2..11 */
    ink_rows(jbm13_glyph('H'), &t, &b);
    CHECK(t == 2 && b == 11);
    /* descenders keep at least two rows under the baseline, inside the cell */
    bad = 0;
    for (const char *p = "gjpqy"; *p; p++) {
        ink_rows(jbm13_glyph((uint8_t)*p), &t, &b);
        if (b < JBM13_BASELINE + 1 || b > JBM13_H - 1) bad++;
    }
    CHECK(bad == 0);
    ink_rows(jbm13_glyph('_'), &t, &b);
    CHECK(b >= JBM13_BASELINE - 1 && b <= JBM13_H - 1);
    /* the tall ones ($ [ ] { }) moved down to fit: top row used, descender kept */
    ink_rows(jbm13_glyph('['), &t, &b);
    CHECK(t == 0 && b >= JBM13_BASELINE);
    /* wide glyphs use the cell edges but fit (@ # % W M) */
    for (const char *p = "@#%WM"; *p; p++) {
        const uint8_t *g = jbm13_glyph((uint8_t)*p);
        int cols = 0;
        for (int x = 0; x < JBM13_W; x++) { int s = 0; for (int y = 0; y < JBM13_H; y++) s += px(g, x, y); if (s) cols++; }
        CHECK(cols >= 7);
    }
    /* no stray alpha: every faint pixel (1-2) touches a stronger one (>= 4) */
    int stray = 0;
    for (int c = 0; c < (int)JBM13_COUNT; c++) {
        const uint8_t *g = font_jbm13[c];
        for (int y = 0; y < JBM13_H; y++)
            for (int x = 0; x < JBM13_W; x++) {
                int v = px(g, x, y);
                if (v == 0 || v > 2) continue;
                int nb = 0;
                for (int yy = y - 1; yy <= y + 1; yy++)
                    for (int xx = x - 1; xx <= x + 1; xx++)
                        if ((yy != y || xx != x) && yy >= 0 && yy < JBM13_H && xx >= 0 && xx < JBM13_W && px(g, xx, yy) > nb) nb = px(g, xx, yy);
                if (nb < 4) stray++;
            }
    }
    CHECK(stray == 0);
    /* the full range of coverage is used (anti-aliased, not 1-bit) */
    int levels[16] = { 0 };
    for (int c = 0x21; c <= 0x7E; c++)
        for (int y = 0; y < JBM13_H; y++) for (int x = 0; x < JBM13_W; x++) levels[px(jbm13_glyph((uint8_t)c), x, y)]++;
    int used = 0;
    for (int i = 0; i < 16; i++) if (levels[i]) used++;
    CHECK(used == 16);
    /* icons: battery fill grows with the level; bolt, play, arrows, bar present */
    for (int l = 0; l < 4; l++) CHECK(ink_total(jbm13_glyph((uint8_t)CH_BATT(l))) < ink_total(jbm13_glyph((uint8_t)CH_BATT(l + 1))));
    const char icons[] = { CH_BOLT, CH_PLAY_R, CH_PLAY_L, CH_UP, CH_DOWN, CH_RIGHT, CH_LEFT, CH_BAR_FULL, CH_BAR_EMPTY };
    for (size_t i = 0; i < sizeof icons; i++) CHECK(ink_total(jbm13_glyph((uint8_t)icons[i])) > 0);
    CHECK(ink_total(jbm13_glyph((uint8_t)CH_BAR_FULL)) > ink_total(jbm13_glyph((uint8_t)CH_BAR_EMPTY)));
    /* the OLED has the same icon codes (its own 5x7 drawings) */
    CHECK(glyph5x7((uint8_t)CH_PLAY_R) != &font5x7[CH_PLAY_R * 5]);
    CHECK(glyph5x7((uint8_t)CH_BOLT) != &font5x7[CH_BOLT * 5]);
    CHECK(glyph5x7((uint8_t)CH_RIGHT) == &font5x7[CH_RIGHT * 5]);   /* CP437 arrow from the font */
    /* routing matrix dots: routed clearly heavier than not routed, both fonts */
    CHECK(ink_total(jbm13_glyph((uint8_t)CH_DOT_OFF)) > 0);
    CHECK(ink_total(jbm13_glyph((uint8_t)CH_DOT_ON)) > 4 * ink_total(jbm13_glyph((uint8_t)CH_DOT_OFF)));
    CHECK(glyph5x7((uint8_t)CH_DOT_ON) != &font5x7[CH_DOT_ON * 5]);
    CHECK(glyph5x7((uint8_t)CH_DOT_OFF) != &font5x7[CH_DOT_OFF * 5]);
    int on5 = 0, off5 = 0;
    for (int k = 0; k < 5; k++) {
        for (int b = 0; b < 8; b++) {
            on5 += (glyph5x7((uint8_t)CH_DOT_ON)[k] >> b) & 1;
            off5 += (glyph5x7((uint8_t)CH_DOT_OFF)[k] >> b) & 1;
        }
    }
    CHECK(off5 >= 1 && on5 > 4 * off5);
}

/* ------------------------------------------------------------------------ */
/* blending and rasterising                                                  */
/* ------------------------------------------------------------------------ */
static uint16_t be(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

static void test_blend(void) {
    printf("tft blend / render\n");
    static const uint16_t cols[] = { 0x0000, 0xFFFF, RGB565(0x1C, 0x48, 0x98), RGB565(0xFF, 0xB0, 0x30), RGB565(0x50, 0xF0, 0x50), 0xF800, 0x07E0, 0x001F };
    const size_t nc = sizeof cols / sizeof cols[0];
    for (size_t i = 0; i < nc; i++)
        for (size_t j = 0; j < nc; j++) {
            uint16_t fg = cols[i], bg = cols[j], r[16];
            tft_ramp(fg, bg, r);
            CHECK(r[0] == bg && r[15] == fg);
            /* each channel moves monotonically from bg to fg */
            bool mono = true;
            for (int a = 1; a < 16; a++) {
                int ch[3][2] = { { r[a] >> 11, r[a - 1] >> 11 }, { (r[a] >> 5) & 63, (r[a - 1] >> 5) & 63 }, { r[a] & 31, r[a - 1] & 31 } };
                int dir[3] = { (fg >> 11) - (bg >> 11), ((fg >> 5) & 63) - ((bg >> 5) & 63), (fg & 31) - (bg & 31) };
                for (int k = 0; k < 3; k++) {
                    if (dir[k] > 0 && ch[k][0] < ch[k][1]) mono = false;
                    if (dir[k] < 0 && ch[k][0] > ch[k][1]) mono = false;
                    if (dir[k] == 0 && ch[k][0] != ch[k][1]) mono = false;
                }
            }
            CHECK(mono);
        }
    /* half way between black and white is mid grey */
    uint16_t mid = tft_blend565(0xFFFF, 0x0000, 8);
    CHECK((mid >> 11) == 17 && ((mid >> 5) & 63) == 34 && (mid & 31) == 17);
    CHECK(tft_blend565(0xFFFF, 0x0000, 99) == 0xFFFF);

    /* attribute colours */
    uint16_t fg, bg;
    tft_attr_colours(CLS_NOTE, &fg, &bg);
    CHECK(fg == tft_class_colour(CLS_NOTE) && bg == TFT_BG_COLOUR);
    tft_attr_colours(CLS_NORMAL | ATTR_INVERSE, &fg, &bg);
    CHECK(fg == 0xFFFF && bg == TFT_INV_BG_COLOUR);
    tft_attr_colours(CLS_ERROR | ATTR_INVERSE, &fg, &bg);
    CHECK(fg == 0xFFFF && bg == TFT_ERR_BG_COLOUR);
    tft_attr_colours(CLS_CC | ATTR_CURSOR, &fg, &bg);
    CHECK(fg == tft_class_colour(CLS_CC) && bg == TFT_CUR_BG_COLOUR);
    CHECK(tft_class_colour(200) == tft_class_colour(CLS_NORMAL));

    /* rasterise: a space is all background, the bar cell solid foreground in
     * its rows, big-endian, line stride = span width */
    cell_t row[40];
    for (int i = 0; i < 40; i++) { row[i].ch = ' '; row[i].attr = CLS_NORMAL; }
    row[1].ch = CH_BAR_FULL; row[1].attr = CLS_NOTE;
    row[2].ch = 'H'; row[2].attr = CLS_NORMAL | ATTR_CURSOR;
    static uint16_t out[40 * TFT_CELL_W * TFT_CELL_H];
    tft_render_cells(row, 0, 3, out);
    const int w = 3 * TFT_CELL_W;
    bool ok = true;
    for (int y = 0; y < TFT_CELL_H; y++) for (int x = 0; x < TFT_CELL_W; x++) if (out[y * w + x] != be(TFT_BG_COLOUR)) ok = false;
    CHECK(ok);
    ok = true;
    for (int y = 3; y < 12; y++) for (int x = 0; x < TFT_CELL_W; x++) if (out[y * w + TFT_CELL_W + x] != be(tft_class_colour(CLS_NOTE))) ok = false;
    CHECK(ok);
    CHECK(out[0 * w + TFT_CELL_W] == be(TFT_BG_COLOUR));
    /* cursor cell: grey bar background, underline in the foreground on the last line */
    CHECK(out[0 * w + 2 * TFT_CELL_W] == be(TFT_CUR_BG_COLOUR));
    ok = true;
    for (int x = 0; x < TFT_CELL_W; x++) if (out[(TFT_CELL_H - 1) * w + 2 * TFT_CELL_W + x] != be(0xFFFF)) ok = false;
    CHECK(ok);
    /* the H has partial (blended) pixels: some value strictly between bg and fg */
    int partial = 0;
    for (int y = 0; y < TFT_CELL_H - 1; y++)
        for (int x = 0; x < TFT_CELL_W; x++) {
            uint16_t v = out[y * w + 2 * TFT_CELL_W + x];
            if (v != be(TFT_CUR_BG_COLOUR) && v != be(0xFFFF)) partial++;
        }
    CHECK(partial > 0);
    /* a span: the same pixels as the whole-row render */
    static uint16_t whole[40 * TFT_CELL_W * TFT_CELL_H];
    tft_render_cells(row, 0, 40, whole);
    tft_render_cells(row, 2, 3, out);
    ok = true;
    for (int y = 0; y < TFT_CELL_H; y++)
        for (int x = 0; x < TFT_CELL_W; x++) if (out[y * TFT_CELL_W + x] != whole[y * 40 * TFT_CELL_W + 2 * TFT_CELL_W + x]) ok = false;
    CHECK(ok);
    /* ramp cache: same colours row after row do not rebuild */
    for (int i = 0; i < 40; i++) { row[i].ch = 'a'; row[i].attr = CLS_CC; }
    tft_render_cells(row, 0, 40, whole);
    uint32_t n0 = tft_ramp_builds();
    tft_render_cells(row, 0, 40, whole);
    CHECK(tft_ramp_builds() == n0);
    row[5].attr = CLS_PC;
    tft_render_cells(row, 0, 40, whole);
    CHECK(tft_ramp_builds() == n0 + 2);       /* to PC and back */

    /* dirty spans */
    cell_t a[40], b[40];
    for (int i = 0; i < 40; i++) { a[i].ch = b[i].ch = 'x'; a[i].attr = b[i].attr = 0; }
    int c0, c1;
    CHECK(!tft_row_span(a, b, 40, &c0, &c1));
    b[7].ch = 'y';
    CHECK(tft_row_span(a, b, 40, &c0, &c1) && c0 == 7 && c1 == 8);
    b[39].attr = 3;
    CHECK(tft_row_span(a, b, 40, &c0, &c1) && c0 == 7 && c1 == 40);
    b[0].ch = 'z';
    CHECK(tft_row_span(a, b, 40, &c0, &c1) && c0 == 0 && c1 == 40);
}

/* ------------------------------------------------------------------------ */
/* console at 40x16, 40x11, 21x8                                             */
/* ------------------------------------------------------------------------ */
static char  s_scr[CON_MAX_ROWS][CON_MAX_COLS + 1];
static uint8_t s_sattr[CON_MAX_ROWS][CON_MAX_COLS];
static int   s_draws;
static int   s_cols;

static void fake_draw(int row, const cell_t *cells) {
    s_draws++;
    for (int c = 0; c < s_cols; c++) { s_scr[row][c] = cells[c].ch; s_sattr[row][c] = cells[c].attr; }
    s_scr[row][s_cols] = 0;
}

static void console_at(int cols, int rows) {
    display_driver_t d = { .name = "fake", .cols = (uint8_t)cols, .rows = (uint8_t)rows, .draw_row = fake_draw };
    s_cols = cols;
    memset(s_scr, 0, sizeof s_scr);
    s_draws = 0;
    con_init(&d);
    CHECK(con_cols() == cols && con_rows() == rows);
    con_flush_all();
    CHECK(s_draws == rows);                     /* init: every row once */
    s_draws = 0;
    con_flush_all();
    CHECK(s_draws == 0);
    /* left / right text, padded to the width */
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, "Logs", "1% used");
    con_flush_all();
    CHECK(s_draws == 1);
    CHECK((int)strlen(s_scr[0]) == cols && !strncmp(s_scr[0], "Logs", 4) && !strcmp(s_scr[0] + cols - 7, "1% used"));
    CHECK(s_sattr[0][cols - 1] == (CLS_HEADER | ATTR_INVERSE));
    /* clipping at the right edge, last row reachable, rows past the grid ignored */
    char longs[80];
    memset(longs, 'x', sizeof longs - 1);
    longs[sizeof longs - 1] = 0;
    s_draws = 0;
    con_row(rows - 1, CLS_DIM, longs);
    con_row(rows, CLS_DIM, "ignored");
    con_print(1, cols - 2, CLS_NOTE, "abcdef");
    con_flush_all();
    CHECK(s_draws == 2);
    CHECK((int)strspn(s_scr[rows - 1], "x") == cols);
    CHECK(s_scr[1][cols - 2] == 'a' && s_scr[1][cols - 1] == 'b');
    /* unchanged writes do not dirty the row */
    s_draws = 0;
    con_row(rows - 1, CLS_DIM, longs);
    con_flush_all();
    CHECK(s_draws == 0);
    /* bitmap rows are left alone, and redrawn when handed back */
    con_set_gfx_rows(0x6u);
    con_row(1, 0, "gfx");
    con_row(3, 0, "text");
    s_draws = 0;
    con_flush_all();
    CHECK(s_draws == 1);
    con_set_gfx_rows(0);
    s_draws = 0;
    con_flush_all();
    CHECK(s_draws == 2 && !strncmp(s_scr[1], "gfx", 3));
    con_invalidate();
    s_draws = 0;
    con_flush_all();
    CHECK(s_draws == rows);
    con_init(NULL);
}

static void test_console(void) {
    printf("console 40x16 / 40x11 / 21x8\n");
    CHECK(CON_MAX_COLS >= 40 && CON_MAX_ROWS >= 16 && CON_MAX_ROWS <= 32);
    console_at(40, 16);
    console_at(40, 11);
    console_at(21, 8);
    /* the TFT geometries the driver derives (320 / 8, 240 / 15, 170 / 15) */
    CHECK(320 / TFT_CELL_W == 40 && 240 / TFT_CELL_H == 16 && 170 / TFT_CELL_H == 11);
}

/* ------------------------------------------------------------------------ */
/* decode widths at the monitor widths                                       */
/* ------------------------------------------------------------------------ */
static void test_widths(void) {
    printf("decode widths\n");
    /* monitor: cols - 2 (tag) - time column: TFT 40 cols: time of day 25,
     * uptime 28, off 38; OLED 21 cols: 10, 9, 19 */
    static const unsigned W[] = { 9, 10, 19, 25, 28, 38 };
    static const uint8_t msc[][24] = {
        { 13, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0x00, '1', 0xF7 },
        { 20, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, 0x32, 0x33, 0x35, 0x2E, 0x36, 0x00, 0x33, 0x36, 0x2E, 0x36, 0x00, 0x35, 0x39, 0xF7 },
        { 13, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0x61, 0x02, 0x03, 0x00, 0x00, '4', 0xF7 },
        { 8, 0xF0, 0x7F, 0x72, 0x02, 0x10, 0x01, '7', 0xF7 },
        { 11, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x06, 0x03, 0x00, 0x7F, 0x01, 0xF7 },
    };
    char b[MIDI_TEXT_MAX + 8];
    for (size_t i = 0; i < sizeof msc / sizeof msc[0]; i++) {
        msc_msg_t d;
        CHECK(msc_decode(&msc[i][1], msc[i][0], msc[i][0], &d));
        size_t prev = 0;
        for (size_t k = 0; k < sizeof W / sizeof W[0]; k++) {
            msc_summary(&d, W[k], b, sizeof b);
            CHECK(strlen(b) <= W[k]);
            CHECK(strlen(b) >= prev);           /* wider never shows less */
            prev = strlen(b);
        }
    }
    /* at the TFT time-of-day width the usual cues are complete (no '~') */
    msc_msg_t d;
    CHECK(msc_decode(&msc[0][1], 13, 13, &d));
    msc_summary(&d, 25, b, sizeof b);
    CHECK_STR(b, "MSC LTG GO Q12.5 L1 @1");
    CHECK(msc_decode(&msc[1][1], 20, 20, &d));
    msc_summary(&d, 25, b, sizeof b);
    CHECK_STR(b, "MSC GO Q235.6 L36.6 P59");
    msc_summary(&d, 28, b, sizeof b);
    CHECK_STR(b, "MSC LTG GO Q235.6 L36.6 P59");
    CHECK(msc_decode(&msc[2][1], 13, 13, &d));
    msc_summary(&d, 28, b, sizeof b);
    CHECK_STR(b, "MSC LTG TGO Q4 01:02:03:00");
    /* every channel / system message fits the TFT time-of-day width */
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    decode_opts_t o = { .note_c3 = false, .width = 25 };
    int over = 0;
    for (int t = 0; t < MT_COUNT; t++) {
        m.type = (uint8_t)t; m.status = 0x9F; m.d1 = 127; m.d2 = 127;
        if (t == MT_SYSEX) { m.sx_len = 300; m.sx[0] = 0xF0; m.sx[1] = 0x00; m.sx[2] = 0x21; m.sx[3] = 0x09; }
        midi_decode(&m, &o, b, sizeof b);
        if (strlen(b) > 25) over++;
    }
    CHECK(over == 0);
    CHECK(MIDI_TEXT_MAX >= CON_MAX_COLS + 1);
}

/* ------------------------------------------------------------------------ */
/* MTC: two inputs, source selection, frame arithmetic                      */
/* ------------------------------------------------------------------------ */
/* a running forward MTC feed at 25 fps */
typedef struct { mtc_t m; int k; mtc_tc_t tc; } feed_t;

static void feed_init(feed_t *f, uint8_t hour) {
    mtc_init(&f->m);
    f->k = 0;
    f->tc = (mtc_tc_t){ hour, 0, 0, 0 };
}

/* quarter frames for `ms` from t (us); returns the end time */
static uint64_t run_mtc(feed_t *f, uint64_t t, unsigned ms) {
    const uint64_t per = 10000;                   /* 25 fps: 100 quarter frames / s */
    for (uint64_t e = t + (uint64_t)ms * 1000u; t < e; t += per) {
        const mtc_tc_t *tc = &f->tc;
        uint8_t v;
        switch (f->k) {
        case 0: v = tc->f & 15; break; case 1: v = (uint8_t)(tc->f >> 4); break;
        case 2: v = tc->s & 15; break; case 3: v = (uint8_t)(tc->s >> 4); break;
        case 4: v = tc->m & 15; break; case 5: v = (uint8_t)(tc->m >> 4); break;
        case 6: v = tc->h & 15; break; default: v = (uint8_t)((tc->h >> 4) | (MTC_RATE_25 << 1)); break;
        }
        mtc_quarter_frame(&f->m, (uint8_t)((f->k << 4) | v), t);
        if (++f->k == 8) { f->k = 0; mtc_tc_inc(&f->tc, MTC_RATE_25); mtc_tc_inc(&f->tc, MTC_RATE_25); }
    }
    return t;
}

static void test_mtc_select(void) {
    printf("mtc source selection\n");
    feed_t FA, FB;
    feed_init(&FA, 1);
    feed_init(&FB, 10);
    mtc_t *const pa = &FA.m, *const pb = &FB.m;
    const mtc_t *in[2] = { pa, pb };
    mtc_sel_t s;
    mtc_sel_init(&s);
    CHECK(mtc_select(&s, in, MTC_SRC_AUTO, 1000) == -1);
    CHECK(mtc_select(&s, in, MTC_SRC_A, 1000) == 0);
    CHECK(mtc_select(&s, in, MTC_SRC_B, 1000) == 1);
    /* A starts first, then both run interleaved: auto stays on A every frame */
    uint64_t t = 1000000;
    t = run_mtc(&FA, t, 400);
    CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 0);
    int flips = 0, last = 0;
    for (int i = 0; i < 200; i++) {
        uint64_t ta = run_mtc(&FA, t, 5);
        run_mtc(&FB, t + 2500, 5);
        t = ta + 2500;
        int now = mtc_select(&s, in, MTC_SRC_AUTO, t);
        if (now != last) flips++;
        last = now;
    }
    CHECK(flips == 0 && last == 0);
    /* forced B shows B; going back to auto keeps the latch (A) */
    CHECK(mtc_select(&s, in, MTC_SRC_B, t) == 1);
    CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 0);
    /* A stops, B keeps running: still A for 2 s (stopped, held) ... */
    uint64_t a_stop = t;
    for (int i = 0; i < 19; i++) { t = run_mtc(&FB, t, 100); CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 0); }
    CHECK(!mtc_running(pa, t) && mtc_active(pa, t));
    /* ... then B */
    t = run_mtc(&FB, t, 200);
    CHECK(t - a_stop >= MTC_HOLD_US);
    CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 1);
    /* A starts again: B stays (latched while active) */
    for (int i = 0; i < 20; i++) {
        uint64_t ta = run_mtc(&FA, t, 50);
        t = run_mtc(&FB, t, 50);
        if (ta > t) t = ta;
        CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 1);
    }
    /* everything silent for long: the latch stays (display shows the last) */
    t += 10000000;
    CHECK(mtc_select(&s, in, MTC_SRC_AUTO, t) == 1);
    /* a fresh selector, nothing active: the most recent input, not latched */
    mtc_sel_t s2;
    mtc_sel_init(&s2);
    CHECK(mtc_select(&s2, in, MTC_SRC_AUTO, t) == (mtc_last_us(pb) > mtc_last_us(pa) ? 1 : 0));
    CHECK(s2.cur == -1);

    /* frame arithmetic */
    mtc_tc_t x = { 1, 0, 0, 0 }, y = { 0, 59, 59, 24 };
    CHECK(mtc_tc_frames(&x, MTC_RATE_25) == 90000);
    CHECK(mtc_tc_diff(&x, &y, MTC_RATE_25) == 1);
    CHECK(mtc_tc_diff(&y, &x, MTC_RATE_25) == -1);
    mtc_tc_t z0 = { 0, 0, 0, 0 }, z1 = { 23, 59, 59, 24 };
    CHECK(mtc_tc_diff(&z0, &z1, MTC_RATE_25) == 1);                 /* across midnight */
    /* drop-frame: 00:01:00;02 is the 1800th frame (labels ;00 ;01 skipped) */
    mtc_tc_t d1 = { 0, 1, 0, 2 }, d0 = { 0, 0, 59, 29 };
    CHECK(mtc_tc_frames(&d1, MTC_RATE_2997) == 1800);
    CHECK(mtc_tc_diff(&d1, &d0, MTC_RATE_2997) == 1);
    mtc_tc_t d10 = { 0, 10, 0, 0 };
    CHECK(mtc_tc_frames(&d10, MTC_RATE_2997) == 17982);              /* 10 min of 29.97 */
    mtc_tc_t h1 = { 1, 0, 0, 0 };
    CHECK(mtc_tc_frames(&h1, MTC_RATE_2997) == 107892);
    /* every valid drop-frame label maps to consecutive frames */
    mtc_tc_t c = { 0, 0, 0, 0 };
    int32_t prev = mtc_tc_frames(&c, MTC_RATE_2997);
    bool consec = true;
    for (int i = 0; i < 40000; i++) {
        mtc_tc_inc(&c, MTC_RATE_2997);
        int32_t f = mtc_tc_frames(&c, MTC_RATE_2997);
        if (f != prev + 1) consec = false;
        prev = f;
    }
    CHECK(consec);
}

/* ------------------------------------------------------------------------ */
/* monitor history: freeze on pause, scrolling                               */
/* ------------------------------------------------------------------------ */
static void push_n(msg_hist_t *h, int n, uint32_t *tag) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    for (int i = 0; i < n; i++) {
        m.type = MT_CC;
        m.t_us = (*tag)++;           /* the message's own sequence, to check identity */
        hist_push(h, &m);
    }
}

static void test_mon_view(void) {
    printf("monitor pause / scroll\n");
    msg_hist_t *h = calloc(1, sizeof *h);
    mon_view_t v = { 0 };
    uint32_t tag = 0, seq[CON_MAX_ROWS];
    uint8_t fl[CON_MAX_ROWS];
    const int R = 15;                        /* 2.8": 16 rows - header */
    hist_clear(h);
    CHECK(mv_up(&v, h, true, R, 1) == MV_NONE && !v.paused);   /* nothing yet */
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(fl[R - 1] & MV_ROW_EMPTY);

    push_n(h, 1000, &tag);                   /* the ring has wrapped many times */
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(seq[R - 1] == 999 && (fl[R - 1] & MV_ROW_PIN) && seq[R - 2] == 998 && seq[0] == 999 - 14);

    /* first UP pauses: cursor on the pinned newest line, view frozen */
    CHECK(mv_up(&v, h, true, R, 1) == MV_PAUSED && v.paused && v.cursor == 999);
    uint32_t before[CON_MAX_ROWS];
    mv_layout(&v, h, true, R, before, fl);
    CHECK(fl[R - 1] & MV_ROW_CURSOR);
    push_n(h, 5000, &tag);                   /* a dense stream keeps arriving */
    CHECK(hist_new_since(h) == 5000 && hist_live_next(h) == 6000);
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(!memcmp(seq, before, sizeof(uint32_t) * R));       /* nothing moved */
    midi_msg_t m;
    CHECK(hist_get(h, 999, &m) && m.t_us == 999);           /* the frozen copy */
    CHECK(hist_get(h, 999 - 127, &m) && m.t_us == 999 - 127);
    CHECK(!hist_get(h, 999 - 128, &m));
    CHECK(!hist_get(h, 1000, &m));

    /* scroll up 60 lines one by one, the view follows the cursor */
    for (int i = 0; i < 60; i++) {
        CHECK(mv_up(&v, h, true, R, 1) == MV_MOVED);
        push_n(h, 37, &tag);
    }
    CHECK(v.cursor == 999 - 60);
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(seq[0] == v.cursor && (fl[0] & MV_ROW_CURSOR));   /* cursor on the top row */
    CHECK(seq[R - 1] == 999);                               /* pinned line still the frozen newest */
    /* held key: big steps clamp at the oldest, then TOP */
    CHECK(mv_up(&v, h, true, R, 8) == MV_MOVED && v.cursor == 999 - 68);
    for (int i = 0; i < 20; i++) mv_up(&v, h, true, R, 8);
    CHECK(v.cursor == hist_oldest(h) && v.cursor == 999 - 127);
    CHECK(mv_up(&v, h, true, R, 1) == MV_TOP);
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(seq[0] == 999 - 127 && (fl[0] & MV_ROW_CURSOR));
    /* DOWN back to the pinned line, then once more: live again */
    int downs = 0;
    while (v.paused && downs < 200) { mv_down(&v, h, true, R); downs++; }
    CHECK(!v.paused && downs == 128);
    CHECK(hist_next(h) == hist_live_next(h));
    mv_layout(&v, h, true, R, seq, fl);
    CHECK(seq[R - 1] == hist_live_next(h) - 1);
    /* LEFT resumes too */
    mv_up(&v, h, true, R, 1);
    mv_up(&v, h, true, R, 1);
    mv_resume(&v, h);
    CHECK(!v.paused && !h->paused);
    CHECK(mv_down(&v, h, true, R) == MV_NONE);

    /* the selected message is the frozen one */
    uint32_t sel;
    mv_up(&v, h, true, R, 1);
    mv_up(&v, h, true, R, 3);
    push_n(h, 400, &tag);
    CHECK(mv_selected(&v, h, &sel) && hist_get(h, sel, &m) && m.t_us == sel);
    CHECK(sel == hist_next(h) - 4);
    mv_resume(&v, h);

    /* unpinned: UP pauses with the cursor on the bottom row */
    CHECK(mv_up(&v, h, false, R, 1) == MV_PAUSED);
    uint32_t newest = hist_next(h) - 1;
    mv_layout(&v, h, false, R, seq, fl);
    CHECK(seq[R - 1] == newest && (fl[R - 1] & MV_ROW_CURSOR) && !(fl[R - 1] & MV_ROW_PIN));
    for (int i = 0; i < 20; i++) mv_up(&v, h, false, R, 1);
    push_n(h, 3000, &tag);
    mv_layout(&v, h, false, R, seq, fl);
    CHECK(seq[0] == newest - 20 && (fl[0] & MV_ROW_CURSOR));
    for (int i = 0; i < 20; i++) CHECK(mv_down(&v, h, false, R) == MV_MOVED);
    CHECK(mv_down(&v, h, false, R) == MV_RESUMED && !v.paused);

    /* clearing resumes */
    mv_up(&v, h, true, R, 1);
    hist_clear(h);
    v.paused = false;
    CHECK(!h->paused && hist_next(h) == 0);
    /* short history: pinned with one message only, UP pauses on it, DOWN resumes */
    push_n(h, 1, &tag);
    CHECK(mv_up(&v, h, true, R, 1) == MV_PAUSED);
    CHECK(mv_up(&v, h, true, R, 1) == MV_TOP);
    CHECK(mv_down(&v, h, true, R) == MV_RESUMED);
    /* 1.9" and OLED row counts */
    push_n(h, 50, &tag);
    mv_layout(&v, h, true, 10, seq, fl);
    CHECK(seq[9] == hist_next(h) - 1 && seq[0] == hist_next(h) - 10);
    mv_layout(&v, h, true, 7, seq, fl);
    CHECK(seq[6] == hist_next(h) - 1 && seq[0] == hist_next(h) - 7);
    free(h);
}

void test_v6(void) {
    test_font();
    test_blend();
    test_console();
    test_widths();
    test_mtc_select();
    test_mon_view();
}
