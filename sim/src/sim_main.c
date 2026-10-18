/*
 * sim_main.c - browser simulator entry points (emscripten).
 *
 * Boots exactly like src/main.c (same order, same calls) and then runs the
 * same core0 loop body. core1's engine loop (sim_io.c) and the timer IRQs
 * (joystick sampling, MIDI clock) run interleaved on the simulated clock.
 *
 * Exported C API (see sim/web for the page):
 *   sim_init(kind)        power on: 1 = OLED fitted, 2 = TFT (auto-detect sees it)
 *   sim_tick(ms)          run ms of simulated time
 *   sim_key(k, down)      joystick contact 0 up 1 down 2 left 3 right 4 press
 *   sim_midi_in(p, b)     byte onto IN A (0) / IN B (1) as soon as the wire is free
 *   sim_midi_in_at(p,b,t) same, not before simulated time t (us)
 *   sim_out_read(p, buf, times, max)  bytes that left OUT A / OUT B
 *   sim_framebuffer() / sim_fb_w() / sim_fb_h() / sim_fb_version()
 */
#include <emscripten/emscripten.h>
#include <string.h>

#include "sim_hal.h"
#include "sim_io.h"
#include "pins.h"
#include "config.h"
#include "settings.h"
#include "midi_io.h"
#include "display.h"
#include "joystick.h"
#include "monitor.h"
#include "generator.h"
#include "ui.h"
#include "debug_log.h"

#define CORE0_LOOP_US 100u   /* how often the sim runs one core0 loop pass */

static int s_kind;
static bool s_in_loop;
static uint64_t s_splash_until;
static const display_driver_t *s_drv;

/* As src/main.c (static there). */
static void splash(void) {
    con_clear();
    con_row(1, CLS_HEADER, "  " MIDIUM_NAME);
    con_row(2, CLS_DIM, "  " MIDIUM_MODEL " v" MIDIUM_VERSION);
    con_row(4, CLS_NORMAL, "  inline MIDI tester");
    con_flush_all();
}

static const display_driver_t *select_display(void) {
    /* MIDIUM_DISPLAY == DISPLAY_AUTO path: the sim's I2C answers at 0x3C
     * only when an OLED is "fitted". */
    return oled_probe() ? &g_disp_oled : &g_disp_tft;
}

static void core0_loop_pass(void) {
    uint64_t now = time_us_64();
    joy_event_t e;
    while ((e = joystick_get()) != JOY_NONE) ui_event(e);
    bool changed = monitor_poll(512);
    gen_task(now);
    ui_render(now, changed);
    con_flush_step();
}

EMSCRIPTEN_KEEPALIVE int sim_init(int kind) {
    s_kind = kind == SIM_DISP_OLED ? SIM_DISP_OLED : SIM_DISP_TFT;
    sim_hal_reset(s_kind == SIM_DISP_OLED);
    s_in_loop = false;

    /* 1. bypass relays off (no-op here) */
    gpio_init(PIN_BYPASS_RELAY);
    gpio_put(PIN_BYPASS_RELAY, 0);
    gpio_set_dir(PIN_BYPASS_RELAY, GPIO_OUT);

    /* 2. engine + core1 */
    settings_defaults(&g_settings);
    midi_io_init(&g_settings.route);
    midi_io_engage_relays();
    midi_io_start_core1();

    /* 3. core0 */
    dbg_init();
    s_drv = select_display();
    if (!s_drv->init() && s_drv == &g_disp_oled) {
        s_drv = &g_disp_tft;
        s_drv->init();
    }
    con_init(s_drv);
    ui_init();
    splash();

    joystick_init();
    monitor_init();
    gen_init();
    /* main.c: sleep_ms(700) then con_clear() - done by sim_tick so the page
     * can show the splash while core1 already passes MIDI. */
    s_splash_until = time_us_64() + 700000u;
    return s_kind;
}

EMSCRIPTEN_KEEPALIVE void sim_tick(double ms) {
    if (ms <= 0) return;
    if (ms > 250) ms = 250;   /* a backgrounded tab must not stall the page */
    uint64_t end = time_us_64() + (uint64_t)(ms * 1000.0);
    while (time_us_64() < end) {
        if (!s_in_loop) {
            uint64_t t = s_splash_until < end ? s_splash_until : end;
            sim_advance_to(t);
            if (time_us_64() >= s_splash_until) {
                con_clear();
                dbg_printf("\n%s %s v%s, display: %s\n", MIDIUM_NAME, MIDIUM_MODEL, MIDIUM_VERSION, s_drv->name);
                s_in_loop = true;
            }
            continue;
        }
        uint64_t t = time_us_64() + CORE0_LOOP_US;
        if (t > end) t = end;
        sim_advance_to(t);
        core0_loop_pass();
    }
}

EMSCRIPTEN_KEEPALIVE void sim_key(int key, int down) { sim_hal_set_key(key, down != 0); }

EMSCRIPTEN_KEEPALIVE int sim_midi_in(int port, int b) { return sim_io_rx_at(port, (uint8_t)b, 0); }
EMSCRIPTEN_KEEPALIVE int sim_midi_in_at(int port, int b, double t_us) {
    return sim_io_rx_at(port, (uint8_t)b, t_us > 0 ? (uint64_t)t_us : 0);
}
EMSCRIPTEN_KEEPALIVE double sim_rx_free_at(int port) { return (double)sim_io_rx_free_at(port); }
EMSCRIPTEN_KEEPALIVE int sim_rx_pending(int port) { return (int)sim_io_rx_pending(port); }
EMSCRIPTEN_KEEPALIVE void sim_rx_flush(int port) { sim_io_rx_flush(port); }
EMSCRIPTEN_KEEPALIVE int sim_out_read(int port, uint8_t *buf, double *times, int max) {
    return sim_io_out_read(port, buf, times, max);
}
EMSCRIPTEN_KEEPALIVE double sim_now_us(void) { return (double)time_us_64(); }

EMSCRIPTEN_KEEPALIVE uint8_t *sim_framebuffer(void) { return sim_hal_fb(); }
EMSCRIPTEN_KEEPALIVE int sim_fb_w(void) { return sim_hal_fb_w(); }
EMSCRIPTEN_KEEPALIVE int sim_fb_h(void) { return sim_hal_fb_h(); }
EMSCRIPTEN_KEEPALIVE int sim_fb_version(void) { return (int)sim_hal_fb_version(); }
EMSCRIPTEN_KEEPALIVE int sim_brightness(void) { return sim_hal_brightness(); }
EMSCRIPTEN_KEEPALIVE int sim_display_on(void) { return sim_hal_display_on(); }
EMSCRIPTEN_KEEPALIVE int sim_led(void) { return sim_io_led(); }
EMSCRIPTEN_KEEPALIVE const char *sim_display_name(void) { return s_drv ? s_drv->name : ""; }

/* Routing matrix as the firmware currently has it: bit (out*3 + src). */
EMSCRIPTEN_KEEPALIVE int sim_route_bits(void) {
    int v = 0;
    for (int o = 0; o < ME_NUM_OUTS; o++)
        for (int s = 0; s < ME_NUM_SRCS; s++)
            if (g_settings.route.src[o][s]) v |= 1 << (o * ME_NUM_SRCS + s);
    return v;
}
EMSCRIPTEN_KEEPALIVE int sim_clock_running(void) { return gen_clock_running(); }

/* Engine counters for the page. */
EMSCRIPTEN_KEEPALIVE double sim_stat(int id) {
    switch (id) {
    case 0: return g_engine.in[ME_IN_A].st.bytes_in;
    case 1: return g_engine.in[ME_IN_B].st.bytes_in;
    case 2: return sim_io_out_total(ME_OUT_A);
    case 3: return sim_io_out_total(ME_OUT_B);
    case 4: return (double)g_engine.out[ME_OUT_A].merge_drops[0] + g_engine.out[ME_OUT_A].merge_drops[1];
    case 5: return (double)g_engine.out[ME_OUT_B].merge_drops[0] + g_engine.out[ME_OUT_B].merge_drops[1];
    case 6: return monitor_total_msgs();
    case 7: return g_engine.stats.gen_msgs;
    case 8: return me_out_pending(&g_engine, ME_OUT_A);
    case 9: return me_out_pending(&g_engine, ME_OUT_B);
    default: return 0;
    }
}
