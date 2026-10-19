/*
 * console.c - character-grid console with per-row dirty tracking.
 */
#include <string.h>
#include "display.h"

static const display_driver_t *s_drv;
static cell_t   s_cells[CON_MAX_ROWS][CON_MAX_COLS];
static uint32_t s_dirty;          /* bit per row */
static int      s_next_row;
static uint32_t s_gfx;            /* rows owned by bitmap drawing */

void con_init(const display_driver_t *drv) {
    s_drv = drv;
    con_clear();
    con_invalidate();
}

const display_driver_t *con_driver(void) { return s_drv; }
int con_cols(void) { return s_drv ? s_drv->cols : CON_MAX_COLS; }
int con_rows(void) { return s_drv ? s_drv->rows : CON_MAX_ROWS; }

static inline void put(int row, int col, char ch, uint8_t attr) {
    cell_t *c = &s_cells[row][col];
    if (c->ch != ch || c->attr != attr) {
        c->ch = ch;
        c->attr = attr;
        s_dirty |= 1u << row;
    }
}

void con_print(int row, int col, uint8_t attr, const char *s) {
    if (row < 0 || row >= con_rows()) return;
    for (int x = col; *s && x < con_cols(); x++, s++)
        if (x >= 0) put(row, x, *s, attr);
}

void con_row(int row, uint8_t attr, const char *s) {
    if (row < 0 || row >= con_rows()) return;
    int x = 0;
    for (; s && *s && x < con_cols(); x++, s++) put(row, x, *s, attr);
    for (; x < con_cols(); x++) put(row, x, ' ', attr);
}

void con_row_lr(int row, uint8_t attr, const char *left, const char *right) {
    con_row(row, attr, left);
    int n = (int)strlen(right);
    con_print(row, con_cols() - n, attr, right);
}

void con_clear(void) {
    for (int r = 0; r < CON_MAX_ROWS; r++) con_row(r, 0, "");
}

void con_invalidate(void) { s_dirty = ((1u << CON_MAX_ROWS) - 1u) & ~s_gfx; }

void con_set_gfx_rows(uint32_t mask) {
    s_dirty |= s_gfx & ~mask;        /* rows handed back get their text redrawn */
    s_gfx = mask;
    s_dirty &= ~mask;
}

bool con_flush_step(void) {
    s_dirty &= ~s_gfx;
    if (!s_drv || !s_dirty) return false;
    int rows = s_drv->rows;
    for (int i = 0; i < rows; i++) {
        int r = (s_next_row + i) % rows;
        if (s_dirty & (1u << r)) {
            s_dirty &= ~(1u << r);
            s_drv->draw_row(r, s_cells[r]);
            s_next_row = (r + 1) % rows;
            return true;
        }
    }
    s_dirty = 0;
    return false;
}

void con_flush_all(void) {
    while (con_flush_step()) { }
}
