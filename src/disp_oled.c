/*
 * disp_oled.c - SH1106 / SSD1306 128x64 OLED over I2C1 (400 kHz).
 *
 * Each 8-pixel page is exactly one console row of 6x8 cells, so a row
 * redraw is a single page write (~3 ms at 400 kHz). 21 cells x 6 px = 126 px,
 * centred with 1 px margin. The SH1106 has 132 columns of RAM with the
 * visible 128 starting at column 2, hence the extra offset.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "config.h"
#include "display.h"
#include "font5x7.h"

#if MIDIUM_OLED_SSD1306
#define OLED_COL_OFFSET 0
#else
#define OLED_COL_OFFSET 2
#endif
#define OLED_COLS  21
#define OLED_ROWS  8
#define OLED_XPAD  1

static bool s_bus_ready;

void i2c1_bus_init(void) {
    if (s_bus_ready) return;
    i2c_init(DISPLAY_I2C, I2C_BAUD);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    /* Modules normally carry 4k7-10k pull-ups; the weak internal ones keep
     * the probe deterministic when nothing is fitted. */
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
    s_bus_ready = true;
}

/* Address found by oled_probe(): 0x3C (DC/SA0 low) or 0x3D (DC/SA0 high,
 * the Adafruit 938 default when DC is left floating). */
static uint8_t s_addr = OLED_I2C_ADDR;

bool oled_probe(void) {
    i2c1_bus_init();
    static const uint8_t addrs[] = { OLED_I2C_ADDR, OLED_I2C_ADDR_ALT };
    for (size_t i = 0; i < sizeof addrs; i++) {
        uint8_t dummy;
        if (i2c_read_timeout_us(DISPLAY_I2C, addrs[i], &dummy, 1, false, 5000) == 1) {
            s_addr = addrs[i];
            return true;
        }
    }
    return false;
}

static bool cmds(const uint8_t *c, size_t n) {
    uint8_t buf[32];
    if (n + 1 > sizeof buf) return false;
    buf[0] = 0x00;                      /* Co=0, D/C=0: command stream */
    memcpy(buf + 1, c, n);
    return i2c_write_timeout_us(DISPLAY_I2C, s_addr, buf, n + 1, false, 20000) == (int)(n + 1);
}

static bool oled_init(void) {
    i2c1_bus_init();
    (void)oled_probe();                 /* pick 0x3C or 0x3D, also when forced */
    static const uint8_t init_seq[] = {
        0xAE,             /* display off */
        0xD5, 0x80,       /* clock divide */
        0xA8, 0x3F,       /* multiplex 64 */
        0xD3, 0x00,       /* display offset */
        0x40,             /* start line 0 */
#if MIDIUM_OLED_SSD1306
        0x8D, 0x14,       /* charge pump on */
        0x20, 0x02,       /* page addressing mode */
#else
        0xAD, 0x8B,       /* SH1106 DC-DC on */
#endif
        0xA1,             /* segment remap (column 127 = SEG0) */
        0xC8,             /* COM scan descending */
        0xDA, 0x12,       /* COM pins */
        0x81, 0xCF,       /* contrast */
        0xD9, 0xF1,       /* pre-charge */
        0xDB, 0x40,       /* VCOMH */
        0xA4,             /* resume to RAM content */
        0xA6,             /* normal (not inverted) */
    };
    if (!cmds(init_seq, sizeof init_seq)) return false;

    /* clear all of RAM (132 columns covers SH1106) */
    uint8_t zero[133];
    memset(zero, 0, sizeof zero);
    zero[0] = 0x40;
    for (uint8_t p = 0; p < 8; p++) {
        uint8_t a[3] = { (uint8_t)(0xB0 | p), 0x00, 0x10 };
        cmds(a, 3);
        i2c_write_timeout_us(DISPLAY_I2C, s_addr, zero,
                             MIDIUM_OLED_SSD1306 ? 129 : 133, false, 50000);
    }
    static const uint8_t on[] = { 0xAF };
    return cmds(on, 1);
}

static void oled_draw_row(int row, const cell_t *cells) {
    uint8_t buf[1 + 128];
    memset(buf, 0, sizeof buf);
    buf[0] = 0x40;                      /* data stream */
    uint8_t *px = buf + 1 + OLED_XPAD;
    for (int c = 0; c < OLED_COLS; c++) {
        const uint8_t *g = &font5x7[(uint8_t)cells[c].ch * 5u];
        bool inv = (cells[c].attr & ATTR_INVERSE) != 0;
        bool cur = (cells[c].attr & ATTR_CURSOR) != 0;
        for (int i = 0; i < 6; i++) {
            uint8_t v = (i < 5) ? g[i] : 0x00;
            if (inv) v = (uint8_t)~v;
            if (cur) v ^= 0x80;          /* cursor: underline (a dark line on an inverse bar) */
            px[c * 6 + i] = v;
        }
    }
    /* extend inverse bars into the margins so highlighted rows look solid */
    if (cells[0].attr & ATTR_INVERSE) buf[1] = 0xFF;
    if (cells[OLED_COLS - 1].attr & ATTR_INVERSE) buf[128] = 0xFF;

    uint8_t col = OLED_COL_OFFSET;
    uint8_t a[3] = { (uint8_t)(0xB0 | (row & 7)), (uint8_t)(col & 0x0F), (uint8_t)(0x10 | (col >> 4)) };
    cmds(a, 3);
    i2c_write_timeout_us(DISPLAY_I2C, s_addr, buf, sizeof buf, false, 20000);
}

static void oled_draw_bitmap(int x, int y, int w, int h, const uint8_t *bits, int stride, uint8_t attr) {
    uint8_t buf[1 + 130];
    bool inv = (attr & ATTR_INVERSE) != 0;
    if (x < 0 || w <= 0 || x + w > OLED_COLS * 6 || y < 0 || (y & 7) || (h & 7)) return;
    /* a bitmap that reaches a grid edge also paints that 1-px panel margin */
    int lpad = x == 0 ? OLED_XPAD : 0, rpad = x + w == OLED_COLS * 6 ? 128 - OLED_XPAD - OLED_COLS * 6 : 0;
    for (int page = y / 8; page < (y + h) / 8 && page < OLED_ROWS; page++) {
        int k = 0;
        buf[k++] = 0x40;
        for (int i = 0; i < lpad; i++) buf[k++] = inv ? 0xFF : 0x00;
        for (int i = 0; i < w; i++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                int r = page * 8 + b - y;
                if ((bits[r * stride + (i >> 3)] >> (7 - (i & 7))) & 1u) v |= (uint8_t)(1u << b);
            }
            buf[k++] = inv ? (uint8_t)~v : v;
        }
        for (int i = 0; i < rpad; i++) buf[k++] = inv ? 0xFF : 0x00;
        uint8_t col = (uint8_t)(OLED_COL_OFFSET + OLED_XPAD + x - lpad);
        uint8_t a[3] = { (uint8_t)(0xB0 | page), (uint8_t)(col & 0x0F), (uint8_t)(0x10 | (col >> 4)) };
        cmds(a, 3);
        i2c_write_timeout_us(DISPLAY_I2C, s_addr, buf, (size_t)k, false, 20000);
    }
}

static void oled_set_brightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    uint8_t c[2] = { 0x81, (uint8_t)(1 + (pct * 254u) / 100u) };
    cmds(c, 2);
}

const display_driver_t g_disp_oled = {
    .name = "OLED "
#if MIDIUM_OLED_SSD1306
            "SSD1306",
#else
            "SH1106",
#endif
    .cols = OLED_COLS,
    .rows = OLED_ROWS,
    .init = oled_init,
    .draw_row = oled_draw_row,
    .set_brightness = oled_set_brightness,
    .has_colour = false,
    .cell_w = 6,
    .cell_h = 8,
    .draw_bitmap = oled_draw_bitmap,
};
