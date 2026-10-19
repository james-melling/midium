/*
 * display.h - character-grid console over interchangeable display drivers.
 *
 * The UI only ever writes characters + attributes into a grid of cells. The
 * console tracks which rows changed and the driver redraws only those rows.
 *
 *   OLED SH1106/SSD1306 128x64, 6x8 cells  -> 21 x 8
 *   TFT  ST7789 320x170, 12x16 cells (2x)   -> 26 x 10
 */
#ifndef MIDIUM_DISPLAY_H
#define MIDIUM_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_decode.h"   /* msg_class_t colour classes */

#define CON_MAX_COLS 26
#define CON_MAX_ROWS 10

/* Cell attribute: low 4 bits = colour class (msg_class_t), bit 7 = inverse,
 * bit 6 = cursor (TFT: grey bar; both: underline) */
#define ATTR_INVERSE 0x80u
#define ATTR_CURSOR  0x40u
#define ATTR_CLASS(a) ((a) & 0x0Fu)

typedef struct {
    char    ch;
    uint8_t attr;
} cell_t;

typedef struct {
    const char *name;
    uint8_t cols, rows;
    bool (*init)(void);
    void (*draw_row)(int row, const cell_t *cells);  /* cells[cols] */
    void (*set_brightness)(uint8_t pct);              /* 0..100 */
    bool has_colour;
    uint8_t cell_w, cell_h;                           /* pixels per cell */
    /* Draw a 1-bit bitmap (MSB first, rows of `stride` bytes) at pixel
     * (x, y) of the text grid (0, 0 = top-left of cell 0,0). Set bits in the
     * colour of attr's class, clear bits in the background. OLED: y and h
     * must be multiples of 8 (one page = one text row). */
    void (*draw_bitmap)(int x, int y, int w, int h, const uint8_t *bits, int stride, uint8_t attr);
} display_driver_t;

extern const display_driver_t g_disp_oled;
extern const display_driver_t g_disp_tft;

/* Probe the I2C bus for the OLED (also sets up I2C1 for the shared bus). */
bool oled_probe(void);
/* Set up I2C1 (OLED, RTC; idempotent). */
void i2c1_bus_init(void);

/* ---- console ---- */
void con_init(const display_driver_t *drv);
const display_driver_t *con_driver(void);
int  con_cols(void);
int  con_rows(void);
/* Write text at row/col with attr; clipped at the right edge. */
void con_print(int row, int col, uint8_t attr, const char *s);
/* Write a whole row: text left-aligned, padded with spaces in attr. */
void con_row(int row, uint8_t attr, const char *s);
/* Write a whole row with left text and right-aligned text. */
void con_row_lr(int row, uint8_t attr, const char *left, const char *right);
void con_clear(void);
/* Redraw one dirty row per call (keeps the main loop responsive).
 * Returns true if something was drawn. */
bool con_flush_step(void);
void con_flush_all(void);
void con_invalidate(void);
/* Rows drawn with draw_bitmap instead of text (bit per row): the console
 * leaves them alone. Clearing a row's bit redraws its text. */
void con_set_gfx_rows(uint32_t mask);

#endif /* MIDIUM_DISPLAY_H */
