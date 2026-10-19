/*
 * disp_tft.c - ST7789 IPS TFT on SPI0, used landscape.
 *
 *   2.8" 240x320 (MIDIUM_TFT_PANEL 28): 320 x 240, 40 x 16 cells, no margin
 *   1.9" 170x320 (MIDIUM_TFT_PANEL 19): 320 x 170, 40 x 11 cells, 2 px top /
 *                                        3 px bottom margin
 *
 * Text is JetBrains Mono 13 px, anti-aliased: 8 x 15 px cells, each pixel
 * blended between the cell's background and foreground (tft_render.c). The
 * driver keeps a copy of the cells last sent for each row and only sends
 * the columns that changed (one CASET/RASET window per dirty span; a whole
 * row is 320 x 15 x 2 = 9.6 kB, ~2.1 ms at 37.5 MHz SPI).
 *
 * Offsets: the 2.8" panel fills the controller's 240 x 320 RAM. The 1.9" sits
 * at controller column 35 in portrait; with MADCTL MV set (landscape) that
 * becomes the row offset (TFT_Y_OFFSET = 35, TFT_X_OFFSET = 0).
 *
 * Reset: the Pimoroni Pico Display Pack has no reset pin (PIN_TFT_RST < 0),
 * so init relies on the software reset (SWRESET) alone.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "pins.h"
#include "config.h"
#include "display.h"
#include "tft_render.h"

#define CELL_W     TFT_CELL_W
#define CELL_H     TFT_CELL_H
#define TFT_COLS   (TFT_WIDTH / CELL_W)
#define TFT_ROWS   (TFT_HEIGHT / CELL_H)
#define GRID_W     (TFT_COLS * CELL_W)
#define GRID_H     (TFT_ROWS * CELL_H)
#define GRID_X     ((TFT_WIDTH  - GRID_W) / 2)
#define GRID_Y     ((TFT_HEIGHT - GRID_H) / 2)

#if TFT_COLS > CON_MAX_COLS || TFT_ROWS > CON_MAX_ROWS
#error "TFT grid larger than the console"
#endif

/* One text row of pixels (RGB565, big-endian): 9.6 kB. Also the chunk
 * buffer for screen fills and bitmaps. */
static uint16_t s_line[GRID_W * CELL_H];
/* What each row on the panel shows now; s_shown_ok[r] false = unknown
 * (after init or a bitmap drew over it): send the whole row. */
static cell_t   s_shown[TFT_ROWS][TFT_COLS];
static bool     s_shown_ok[TFT_ROWS];
static uint s_bl_slice, s_bl_chan;

static inline void cs(bool active) { gpio_put(PIN_TFT_CS, !active); }
static inline void dc(bool data)   { gpio_put(PIN_TFT_DC, data); }

static void write_cmd(uint8_t c, const uint8_t *d, size_t n) {
    cs(true);
    dc(false);
    spi_write_blocking(TFT_SPI, &c, 1);
    if (n) {
        dc(true);
        spi_write_blocking(TFT_SPI, d, n);
    }
    cs(false);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += TFT_X_OFFSET; x1 += TFT_X_OFFSET;
    y0 += TFT_Y_OFFSET; y1 += TFT_Y_OFFSET;
    uint8_t ca[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t ra[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    write_cmd(0x2A, ca, 4);   /* CASET */
    write_cmd(0x2B, ra, 4);   /* RASET */
}

static void write_pixels_begin(void) {
    uint8_t c = 0x2C;         /* RAMWR */
    cs(true);
    dc(false);
    spi_write_blocking(TFT_SPI, &c, 1);
    dc(true);
}

static void fill_screen(uint16_t colour) {
    set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    uint16_t be = (uint16_t)((colour >> 8) | (colour << 8));
    for (size_t i = 0; i < sizeof s_line / 2u; i++) s_line[i] = be;
    write_pixels_begin();
    size_t total = (size_t)TFT_WIDTH * TFT_HEIGHT * 2u;
    while (total) {
        size_t n = total < sizeof s_line ? total : sizeof s_line;
        spi_write_blocking(TFT_SPI, (const uint8_t *)s_line, n);
        total -= n;
    }
    cs(false);
    memset(s_shown_ok, 0, sizeof s_shown_ok);
}

static void tft_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    pwm_set_chan_level(s_bl_slice, s_bl_chan, (uint16_t)(pct * 10u));
}

static bool tft_init(void) {
    spi_init(TFT_SPI, TFT_SPI_HZ);
    spi_set_format(TFT_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TFT_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_TFT_CS);  gpio_set_dir(PIN_TFT_CS, GPIO_OUT);  gpio_put(PIN_TFT_CS, 1);
    gpio_init(PIN_TFT_DC);  gpio_set_dir(PIN_TFT_DC, GPIO_OUT);  gpio_put(PIN_TFT_DC, 1);
#if PIN_TFT_RST >= 0
    gpio_init(PIN_TFT_RST); gpio_set_dir(PIN_TFT_RST, GPIO_OUT); gpio_put(PIN_TFT_RST, 1);
#endif

    /* backlight PWM ~37.5 kHz, 0..1000 */
    gpio_set_function(PIN_TFT_BL, GPIO_FUNC_PWM);
    s_bl_slice = pwm_gpio_to_slice_num(PIN_TFT_BL);
    s_bl_chan = pwm_gpio_to_channel(PIN_TFT_BL);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 999);
    pwm_init(s_bl_slice, &cfg, true);
    pwm_set_chan_level(s_bl_slice, s_bl_chan, 0);   /* dark until drawn */

#if PIN_TFT_RST >= 0
    /* hardware reset */
    sleep_ms(5);
    gpio_put(PIN_TFT_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_TFT_RST, 1);
#endif
    sleep_ms(120);

    write_cmd(0x01, NULL, 0);  sleep_ms(150);       /* SWRESET (the only reset on the Pimoroni pack) */
    write_cmd(0x11, NULL, 0);  sleep_ms(120);       /* SLPOUT */
    uint8_t colmod = 0x55;                          /* 16 bit/pixel */
    write_cmd(0x3A, &colmod, 1);
    uint8_t madctl = TFT_MADCTL;
    write_cmd(0x36, &madctl, 1);
    write_cmd(TFT_INVERT ? 0x21 : 0x20, NULL, 0);   /* INVON (IPS panels) / INVOFF */
    write_cmd(0x13, NULL, 0);                       /* NORON */
    fill_screen(TFT_BG_COLOUR);
    write_cmd(0x29, NULL, 0);  sleep_ms(10);        /* DISPON */
    return true;
}

static void tft_draw_row(int row, const cell_t *cells) {
    if (row < 0 || row >= TFT_ROWS) return;
    int c0 = 0, c1 = TFT_COLS;
    if (s_shown_ok[row] && !tft_row_span(cells, s_shown[row], TFT_COLS, &c0, &c1)) return;
    memcpy(&s_shown[row][c0], &cells[c0], (size_t)(c1 - c0) * sizeof(cell_t));
    s_shown_ok[row] = true;
    tft_render_cells(cells, c0, c1, s_line);
    int w = (c1 - c0) * CELL_W;
    uint16_t x0 = (uint16_t)(GRID_X + c0 * CELL_W), y0 = (uint16_t)(GRID_Y + row * CELL_H);
    set_window(x0, y0, (uint16_t)(x0 + w - 1), (uint16_t)(y0 + CELL_H - 1));
    write_pixels_begin();
    spi_write_blocking(TFT_SPI, (const uint8_t *)s_line, (size_t)w * CELL_H * 2u);
    cs(false);
}

static void tft_draw_bitmap(int x, int y, int w, int h, const uint8_t *bits, int stride, uint8_t attr) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > GRID_W || y + h > GRID_H) return;
    uint16_t fg = tft_class_colour(ATTR_CLASS(attr)), bg = TFT_BG_COLOUR;
    if (attr & ATTR_INVERSE) { uint16_t t = fg; fg = bg; bg = t; }
    uint16_t fbe = (uint16_t)((fg >> 8) | (fg << 8)), bbe = (uint16_t)((bg >> 8) | (bg << 8));
    for (int r = y / CELL_H; r <= (y + h - 1) / CELL_H; r++) s_shown_ok[r] = false;
    uint16_t x0 = (uint16_t)(GRID_X + x), y0 = (uint16_t)(GRID_Y + y);
    set_window(x0, y0, (uint16_t)(x0 + w - 1), (uint16_t)(y0 + h - 1));
    write_pixels_begin();
    int rows_per_chunk = (int)(sizeof s_line / 2u) / w;
    for (int r0 = 0; r0 < h; r0 += rows_per_chunk) {
        int nr = h - r0 < rows_per_chunk ? h - r0 : rows_per_chunk;
        uint16_t *p = s_line;
        for (int r = r0; r < r0 + nr; r++)
            for (int i = 0; i < w; i++)
                *p++ = ((bits[r * stride + (i >> 3)] >> (7 - (i & 7))) & 1u) ? fbe : bbe;
        spi_write_blocking(TFT_SPI, (const uint8_t *)s_line, (size_t)(p - s_line) * 2u);
    }
    cs(false);
}

static void tft_power(bool on) {
    if (on) {
        write_cmd(0x11, NULL, 0);  sleep_ms(120);   /* SLPOUT */
        write_cmd(0x29, NULL, 0);                   /* DISPON (backlight: set_brightness) */
    } else {
        pwm_set_chan_level(s_bl_slice, s_bl_chan, 0);
        write_cmd(0x28, NULL, 0);                   /* DISPOFF */
        write_cmd(0x10, NULL, 0);                   /* SLPIN */
    }
}

const display_driver_t g_disp_tft = {
#if MIDIUM_TFT_PANEL == 28
    .name = "TFT 2.8\" ST7789",
#else
    .name = "TFT 1.9\" ST7789",
#endif
    .cols = TFT_COLS,
    .rows = TFT_ROWS,
    .init = tft_init,
    .draw_row = tft_draw_row,
    .set_brightness = tft_set_brightness,
    .has_colour = true,
    .cell_w = CELL_W,
    .cell_h = CELL_H,
    .draw_bitmap = tft_draw_bitmap,
    .power = tft_power,
};
