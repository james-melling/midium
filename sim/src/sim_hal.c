/*
 * sim_hal.c - Pico SDK stand-ins for the browser simulator: simulated clock,
 * repeating timers, GPIO (joystick pins), and ST7789 / SSD1306 controller
 * emulators that turn the real drivers' SPI / I2C traffic into an RGBA
 * framebuffer.
 */
#include <string.h>
#include "sim_hal.h"
#include "pins.h"
#include "config.h"

/* ------------------------------------------------------------------------ */
/* Time                                                                      */
/* ------------------------------------------------------------------------ */
static uint64_t s_now;

uint64_t time_us_64(void) { return s_now; }
void sim_set_time(uint64_t t) { s_now = t; }
void sleep_us(uint64_t us) { sim_advance_to(s_now + us); }
void sleep_ms(uint32_t ms) { sleep_us((uint64_t)ms * 1000u); }

/* ------------------------------------------------------------------------ */
/* Repeating timers                                                          */
/* ------------------------------------------------------------------------ */
#define MAX_TIMERS 8
static repeating_timer_t *s_timers[MAX_TIMERS];

bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t cb, void *user, repeating_timer_t *out) {
    out->delay_us = delay_us;
    out->user_data = user;
    out->callback = cb;
    out->next_us = s_now + (uint64_t)(delay_us < 0 ? -delay_us : delay_us);
    out->active = true;
    for (int i = 0; i < MAX_TIMERS; i++) if (s_timers[i] == out) return true;
    for (int i = 0; i < MAX_TIMERS; i++) if (!s_timers[i]) { s_timers[i] = out; return true; }
    return false;
}

bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t cb, void *user, repeating_timer_t *out) {
    return add_repeating_timer_us((int64_t)delay_ms * 1000, cb, user, out);
}

bool cancel_repeating_timer(repeating_timer_t *t) {
    for (int i = 0; i < MAX_TIMERS; i++)
        if (s_timers[i] == t) { s_timers[i] = NULL; t->active = false; return true; }
    return false;
}

uint64_t sim_hal_next_timer(void) {
    uint64_t n = UINT64_MAX;
    for (int i = 0; i < MAX_TIMERS; i++)
        if (s_timers[i] && s_timers[i]->next_us < n) n = s_timers[i]->next_us;
    return n;
}

void sim_hal_run_timers(uint64_t t) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        repeating_timer_t *rt = s_timers[i];
        int guard = 0;
        while (rt && rt->active && rt->next_us <= t && guard++ < 64) {
            uint64_t target = rt->next_us;
            uint64_t saved = s_now;
            if (s_now < target) s_now = target;      /* callback sees its own fire time */
            bool again = rt->callback(rt);
            s_now = saved;
            if (!again || !rt->active) { cancel_repeating_timer(rt); break; }
            /* Negative delay: period from the previous target (fixed rate).
             * Positive: from the end of the callback. Same as the SDK. */
            int64_t d = rt->delay_us;
            rt->next_us = d < 0 ? target + (uint64_t)(-d) : target + (uint64_t)d;
            rt = s_timers[i];
        }
    }
}

/* ------------------------------------------------------------------------ */
/* GPIO: joystick inputs (active low) + output latches                       */
/* ------------------------------------------------------------------------ */
static uint32_t s_keys_down;   /* bit per joystick pin held */
static const uint8_t KEY_PINS[5] = { PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT, PIN_JOY_PRESS };

void sim_hal_set_key(int idx, bool down) {
    if (idx < 0 || idx > 4) return;
    uint32_t m = 1u << KEY_PINS[idx];
    if (down) s_keys_down |= m; else s_keys_down &= ~m;
}

void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_dir(uint gpio, bool out) { (void)gpio; (void)out; }
void gpio_pull_up(uint gpio) { (void)gpio; }
void gpio_set_function(uint gpio, enum gpio_function fn) { (void)gpio; (void)fn; }
/* Everything reads high (pull-ups) except joystick contacts held closed. */
uint32_t gpio_get_all(void) { return ~s_keys_down; }

/* ------------------------------------------------------------------------ */
/* Framebuffer                                                               */
/* ------------------------------------------------------------------------ */
static uint8_t  s_fb[TFT_WIDTH * TFT_HEIGHT * 4];
static int      s_fb_w = TFT_WIDTH, s_fb_h = TFT_HEIGHT;
static uint32_t s_fb_ver;
static int      s_bright = 0;
static bool     s_disp_on;
static bool     s_oled_fitted;

uint8_t *sim_hal_fb(void) { return s_fb; }
int sim_hal_fb_w(void) { return s_fb_w; }
int sim_hal_fb_h(void) { return s_fb_h; }
uint32_t sim_hal_fb_version(void) { return s_fb_ver; }
int sim_hal_brightness(void) { return s_bright; }
bool sim_hal_display_on(void) { return s_disp_on; }

/* ------------------------------------------------------------------------ */
/* ST7789 over SPI0 (landscape, MADCTL MV; panel rows start at RAM row 35)   */
/* ------------------------------------------------------------------------ */
static struct {
    bool cs, dc;
    uint8_t cmd;
    uint8_t arg[4];
    int nargs;
    uint16_t xs, xe, ys, ye, x, y;
    uint8_t hi;
    bool have_hi;
} T;

static void tft_pixel(uint16_t c) {
    int px = (int)T.x - TFT_X_OFFSET, py = (int)T.y - TFT_Y_OFFSET;
    if (px >= 0 && px < TFT_WIDTH && py >= 0 && py < TFT_HEIGHT && !s_oled_fitted) {
        uint8_t *p = &s_fb[((size_t)py * TFT_WIDTH + (size_t)px) * 4u];
        uint8_t r = (uint8_t)((c >> 11) & 0x1F), g = (uint8_t)((c >> 5) & 0x3F), b = (uint8_t)(c & 0x1F);
        p[0] = (uint8_t)((r << 3) | (r >> 2));
        p[1] = (uint8_t)((g << 2) | (g >> 4));
        p[2] = (uint8_t)((b << 3) | (b >> 2));
        p[3] = 255;
    }
    if (++T.x > T.xe) { T.x = T.xs; if (++T.y > T.ye) T.y = T.ys; }
}

static void tft_byte(uint8_t b) {
    if (!T.dc) {                      /* command */
        T.cmd = b; T.nargs = 0; T.have_hi = false;
        if (b == 0x2C) { T.x = T.xs; T.y = T.ys; }
        if (b == 0x29) { s_disp_on = true; s_fb_ver++; }
        if (b == 0x28) { s_disp_on = false; s_fb_ver++; }
        return;
    }
    switch (T.cmd) {
    case 0x2A: case 0x2B:
        if (T.nargs < 4) T.arg[T.nargs++] = b;
        if (T.nargs == 4) {
            uint16_t s = (uint16_t)(T.arg[0] << 8 | T.arg[1]), e = (uint16_t)(T.arg[2] << 8 | T.arg[3]);
            if (T.cmd == 0x2A) { T.xs = s; T.xe = e; } else { T.ys = s; T.ye = e; }
        }
        break;
    case 0x2C:
        if (!T.have_hi) { T.hi = b; T.have_hi = true; }
        else { tft_pixel((uint16_t)(T.hi << 8 | b)); T.have_hi = false; s_fb_ver++; }
        break;
    default:
        break;
    }
}

uint spi_init(spi_inst_t *spi, uint baud) { (void)spi; return baud; }
void spi_set_format(spi_inst_t *spi, uint bits, spi_cpol_t cpol, spi_cpha_t cpha, spi_order_t order) {
    (void)spi; (void)bits; (void)cpol; (void)cpha; (void)order;
}
int spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len) {
    if (spi != TFT_SPI || !T.cs) return (int)len;
    for (size_t i = 0; i < len; i++) tft_byte(src[i]);
    return (int)len;
}

void gpio_put(uint gpio, bool value) {
    if (gpio == PIN_TFT_CS) { T.cs = !value; if (value) T.have_hi = false; }
    if (gpio == PIN_TFT_DC) T.dc = value;
}

/* Backlight PWM: wrap 999, level = pct * 10 */
static uint s_bl_slice = 99;
uint pwm_gpio_to_slice_num(uint gpio) { return (gpio >> 1) & 7u; }
uint pwm_gpio_to_channel(uint gpio) { return gpio & 1u; }
pwm_config pwm_get_default_config(void) { pwm_config c = { 0, 16, 0xFFFF }; return c; }
void pwm_config_set_clkdiv(pwm_config *c, float div) { c->div = (uint32_t)(div * 16.0f); }
void pwm_config_set_wrap(pwm_config *c, uint16_t wrap) { c->top = wrap; }
void pwm_init(uint slice, pwm_config *c, bool start) { (void)c; (void)start; s_bl_slice = slice; }
void pwm_set_chan_level(uint slice, uint chan, uint16_t level) {
    (void)chan;
    if (slice == s_bl_slice && !s_oled_fitted) { s_bright = level / 10; s_fb_ver++; }
}

/* ------------------------------------------------------------------------ */
/* SSD1306 over I2C1 (page addressing, 128 x 64, white)                      */
/* ------------------------------------------------------------------------ */
static struct {
    uint8_t ram[8][132];
    uint8_t page, col;
    bool on, invert;
    int pending;          /* argument bytes still expected by the last command */
    uint8_t pending_cmd;
} O;

static void oled_paint_page(int page) {
    for (int c = 0; c < 128; c++) {
        uint8_t v = O.ram[page][c + (MIDIUM_OLED_SSD1306 ? 0 : 2)];
        for (int bit = 0; bit < 8; bit++) {
            bool lit = ((v >> bit) & 1u) != (O.invert ? 1u : 0u);
            uint8_t *p = &s_fb[((size_t)(page * 8 + bit) * 128u + (size_t)c) * 4u];
            /* white OLED: lit pixels near-white with a hint of blue, off = black */
            p[0] = lit ? 0xEE : 0x00; p[1] = lit ? 0xF4 : 0x00; p[2] = lit ? 0xFF : 0x00; p[3] = 255;
        }
    }
    s_fb_ver++;
}

static void oled_cmd(uint8_t b) {
    if (O.pending) {
        if (O.pending_cmd == 0x81) { s_bright = (b - 1) * 100 / 254; if (s_bright < 0) s_bright = 0; s_fb_ver++; }
        O.pending--;
        return;
    }
    if (b >= 0xB0 && b <= 0xB7) { O.page = b & 7u; return; }
    if (b <= 0x0F) { O.col = (uint8_t)((O.col & 0xF0) | b); return; }
    if (b >= 0x10 && b <= 0x1F) { O.col = (uint8_t)((O.col & 0x0F) | ((b & 0x0F) << 4)); return; }
    switch (b) {
    case 0xAE: O.on = false; s_disp_on = false; s_fb_ver++; break;
    case 0xAF: O.on = true;  s_disp_on = true;  s_fb_ver++; break;
    case 0xA6: O.invert = false; for (int p = 0; p < 8; p++) oled_paint_page(p); break;
    case 0xA7: O.invert = true;  for (int p = 0; p < 8; p++) oled_paint_page(p); break;
    case 0x81: case 0xD5: case 0xA8: case 0xD3: case 0x8D: case 0x20: case 0xDA:
    case 0xD9: case 0xDB: case 0xAD:
        O.pending = 1; O.pending_cmd = b; break;
    default: break;
    }
}

uint i2c_init(i2c_inst_t *i2c, uint baud) { (void)i2c; return baud; }

int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint timeout_us) {
    (void)nostop; (void)timeout_us;
    if (i2c == DISPLAY_I2C && s_oled_fitted && addr == OLED_I2C_ADDR) {
        memset(dst, 0, len);
        return (int)len;
    }
    return PICO_ERROR_GENERIC;       /* no ACK: nothing at this address */
}

int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint timeout_us) {
    (void)nostop; (void)timeout_us;
    if (i2c != DISPLAY_I2C || !s_oled_fitted || addr != OLED_I2C_ADDR || len == 0) return PICO_ERROR_GENERIC;
    if (src[0] == 0x00) {
        for (size_t i = 1; i < len; i++) oled_cmd(src[i]);
    } else if (src[0] == 0x40) {
        for (size_t i = 1; i < len; i++) {
            if (O.col < 132) O.ram[O.page][O.col] = src[i];
            O.col++;
        }
        oled_paint_page(O.page);
    }
    return (int)len;
}

/* ------------------------------------------------------------------------ */
bool stdio_init_all(void) { return true; }
bool stdio_usb_connected(void) { return false; }

void sim_hal_reset(int oled_fitted) {
    memset(s_timers, 0, sizeof s_timers);
    memset(&T, 0, sizeof T);
    memset(&O, 0, sizeof O);
    memset(s_fb, 0, sizeof s_fb);
    for (size_t i = 3; i < sizeof s_fb; i += 4) s_fb[i] = 255;
    s_oled_fitted = oled_fitted != 0;
    s_fb_w = s_oled_fitted ? 128 : TFT_WIDTH;
    s_fb_h = s_oled_fitted ? 64 : TFT_HEIGHT;
    s_bright = 0;
    s_disp_on = false;
    s_keys_down = 0;
    s_now = 0;
    s_fb_ver++;
}
