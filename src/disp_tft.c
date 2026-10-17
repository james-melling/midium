/*
 * disp_tft.c - ST7789 1.9" 170x320 IPS TFT on SPI0, used landscape 320x170.
 *
 * Console cells are the 5x7 font scaled 2x in a 12x16 cell: 26 x 10 cells =
 * 312 x 160 px, centred (4 px left/right, 5 px top/bottom).
 *
 * Offsets: in portrait the 170-px-wide panel sits at controller column 35.
 * With MADCTL MV set (landscape) the controller swaps rows/columns, so the
 * offset moves to the row address (TFT_Y_OFFSET = 35, TFT_X_OFFSET = 0).
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "pins.h"
#include "config.h"
#include "display.h"
#include "midi_decode.h"
#include "font5x7.h"

#define TFT_COLS   26
#define TFT_ROWS   10
#define CELL_W     12
#define CELL_H     16
#define GRID_X     ((TFT_WIDTH  - TFT_COLS * CELL_W) / 2)
#define GRID_Y     ((TFT_HEIGHT - TFT_ROWS * CELL_H) / 2)

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t s_fg[CLS_COUNT] = {
    [CLS_NORMAL]   = RGB565(0xFF, 0xFF, 0xFF),
    [CLS_NOTE]     = RGB565(0x40, 0xFF, 0x40),
    [CLS_CC]       = RGB565(0x40, 0xE0, 0xFF),
    [CLS_PC]       = RGB565(0xFF, 0xE0, 0x30),
    [CLS_SYSEX]    = RGB565(0xFF, 0x50, 0xFF),
    [CLS_REALTIME] = RGB565(0x90, 0x90, 0x90),
    [CLS_ERROR]    = RGB565(0xFF, 0x40, 0x40),
    [CLS_HEADER]   = RGB565(0xFF, 0xA0, 0x20),
    [CLS_DIM]      = RGB565(0x80, 0x80, 0x80),
};
#define BG_COLOUR      RGB565(0x00, 0x00, 0x00)
#define INV_BG_COLOUR  RGB565(0x20, 0x50, 0xA0)   /* highlighted menu row */

static uint8_t s_line[TFT_COLS * CELL_W * CELL_H * 2];   /* one text row, RGB565 BE */
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
    for (size_t i = 0; i < sizeof s_line; i += 2) {
        s_line[i] = (uint8_t)(colour >> 8);
        s_line[i + 1] = (uint8_t)colour;
    }
    write_pixels_begin();
    size_t total = (size_t)TFT_WIDTH * TFT_HEIGHT * 2u;
    while (total) {
        size_t n = total < sizeof s_line ? total : sizeof s_line;
        spi_write_blocking(TFT_SPI, s_line, n);
        total -= n;
    }
    cs(false);
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
    gpio_init(PIN_TFT_RST); gpio_set_dir(PIN_TFT_RST, GPIO_OUT); gpio_put(PIN_TFT_RST, 1);

    /* backlight PWM ~37.5 kHz, 0..1000 */
    gpio_set_function(PIN_TFT_BL, GPIO_FUNC_PWM);
    s_bl_slice = pwm_gpio_to_slice_num(PIN_TFT_BL);
    s_bl_chan = pwm_gpio_to_channel(PIN_TFT_BL);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.0f);
    pwm_config_set_wrap(&cfg, 999);
    pwm_init(s_bl_slice, &cfg, true);
    pwm_set_chan_level(s_bl_slice, s_bl_chan, 0);   /* dark until drawn */

    /* hardware reset */
    sleep_ms(5);
    gpio_put(PIN_TFT_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_TFT_RST, 1);
    sleep_ms(120);

    write_cmd(0x01, NULL, 0);  sleep_ms(150);       /* SWRESET */
    write_cmd(0x11, NULL, 0);  sleep_ms(120);       /* SLPOUT */
    uint8_t colmod = 0x55;                          /* 16 bit/pixel */
    write_cmd(0x3A, &colmod, 1);
    uint8_t madctl = TFT_MADCTL;
    write_cmd(0x36, &madctl, 1);
    write_cmd(0x21, NULL, 0);                       /* INVON (IPS panels) */
    write_cmd(0x13, NULL, 0);                       /* NORON */
    fill_screen(BG_COLOUR);
    write_cmd(0x29, NULL, 0);  sleep_ms(10);        /* DISPON */
    return true;
}

static void tft_draw_row(int row, const cell_t *cells) {
    const int w = TFT_COLS * CELL_W;
    for (int c = 0; c < TFT_COLS; c++) {
        uint8_t attr = cells[c].attr;
        uint8_t cls = ATTR_CLASS(attr);
        uint16_t fg = s_fg[cls < CLS_COUNT ? cls : 0];
        uint16_t bg = BG_COLOUR;
        if (attr & ATTR_INVERSE) { bg = INV_BG_COLOUR; if (cls == CLS_NORMAL) fg = 0xFFFF; }
        const uint8_t *g = &font5x7[(uint8_t)cells[c].ch * 5u];
        for (int y = 0; y < CELL_H; y++) {
            uint8_t *p = &s_line[((size_t)y * w + (size_t)c * CELL_W) * 2u];
            uint8_t bit = (uint8_t)(1u << (y >> 1));
            for (int x = 0; x < CELL_W; x++) {
                int gx = x >> 1;
                uint16_t col = (gx < 5 && (g[gx] & bit)) ? fg : bg;
                *p++ = (uint8_t)(col >> 8);
                *p++ = (uint8_t)col;
            }
        }
    }
    uint16_t x0 = GRID_X, y0 = (uint16_t)(GRID_Y + row * CELL_H);
    set_window(x0, y0, (uint16_t)(x0 + w - 1), (uint16_t)(y0 + CELL_H - 1));
    write_pixels_begin();
    spi_write_blocking(TFT_SPI, s_line, (size_t)w * CELL_H * 2u);
    cs(false);
}

const display_driver_t g_disp_tft = {
    .name = "TFT ST7789",
    .cols = TFT_COLS,
    .rows = TFT_ROWS,
    .init = tft_init,
    .draw_row = tft_draw_row,
    .set_brightness = tft_set_brightness,
    .has_colour = true,
};
