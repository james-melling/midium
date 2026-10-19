/*
 * sim_io.c - simulator replacement for midi_io.c.
 *
 * Same job as core1_main() on the RP2350, single-threaded: bytes arrive on
 * IN A / IN B at MIDI wire rate (31250 baud, 10 bits = 320 us per byte),
 * each is given to the real engine (me_input_byte) and the monitor ring
 * (me_mon_push), me_service() merges generated traffic, and each output is
 * paced like the UART: a byte is popped only when the TX FIFO is empty and
 * leaves the shift register 320 us later. Bytes leaving OUT A / OUT B are
 * captured with their start-of-byte timestamp for the page.
 *
 * Power: midi_io_shutdown() stops "core1" and drops the relays, as on the
 * RP2350; with the relays dropped (or the supply cut) IN A is wired to
 * OUT A, so its bytes are copied to OUT A unchanged (passive thru) and IN B
 * goes nowhere.
 */
#include <string.h>
#include "sim_hal.h"
#include "midi_io.h"
#include "sim_io.h"
#include "pins.h"

me_t g_engine;

#define BYTE_US      320u
#define CORE1_STEP   40u          /* max core1 loop granularity in the sim */
#define LED_HOLD_US  30000u

/* ---- input scheduling ---- */
#define RXQ 65536u
typedef struct { uint64_t not_before; uint8_t b; } rx_ent_t;
static rx_ent_t s_rx[ME_NUM_INS][RXQ];
static uint32_t s_rxh[ME_NUM_INS], s_rxt[ME_NUM_INS];
static uint64_t s_line_free[ME_NUM_INS];   /* when the wire is idle again */
static uint64_t s_sched_end[ME_NUM_INS];   /* end of the last scheduled byte */

/* ---- output pacing / capture ---- */
static bool     s_fifo_full[ME_NUM_OUTS];
static uint8_t  s_fifo[ME_NUM_OUTS];
static uint64_t s_shift_end[ME_NUM_OUTS];
static uint64_t s_fifo_t[ME_NUM_OUTS];      /* when core1 wrote the FIFO */
#define CAPQ 16384u
static uint8_t  s_cap[ME_NUM_OUTS][CAPQ];
static double   s_cap_t[ME_NUM_OUTS][CAPQ];
static uint32_t s_caph[ME_NUM_OUTS], s_capt[ME_NUM_OUTS];
static uint32_t s_cap_total[ME_NUM_OUTS];

static uint32_t s_last_rx_ms;
static uint64_t s_led_until;
static uint64_t s_core1_now;
static bool     s_core1_running;
static bool     s_relays;
static bool     s_reboot;
static bool     s_soft_wake;
static uint32_t s_rx_total[ME_NUM_INS];

void midi_io_init(me_settings_t *settings) {
    me_init(&g_engine, settings);
    memset(s_rxh, 0, sizeof s_rxh); memset(s_rxt, 0, sizeof s_rxt);
    memset(s_line_free, 0, sizeof s_line_free); memset(s_sched_end, 0, sizeof s_sched_end);
    memset(s_fifo_full, 0, sizeof s_fifo_full); memset(s_shift_end, 0, sizeof s_shift_end);
    memset(s_caph, 0, sizeof s_caph); memset(s_capt, 0, sizeof s_capt);
    memset(s_cap_total, 0, sizeof s_cap_total); memset(s_rx_total, 0, sizeof s_rx_total);
    s_last_rx_ms = 0; s_led_until = 0; s_core1_running = false;
    s_core1_now = time_us_64();
}

void midi_io_engage_relays(void) { sleep_ms(5); gpio_put(PIN_BYPASS_RELAY, 1); s_relays = true; }
void midi_io_start_core1(void) { s_core1_now = time_us_64(); s_core1_running = true; s_reboot = false; }
uint32_t midi_io_last_rx_ms(void) { return s_last_rx_ms; }
bool midi_io_watchdog_reset(void) { return false; }

/* As src/midi_io.c: drain (bounded), stop core1, drop the relays. */
void midi_io_shutdown(void) {
    uint64_t t0 = time_us_64();
    while (time_us_64() - t0 < 100000u &&
           (me_out_pending(&g_engine, ME_OUT_A) || me_out_pending(&g_engine, ME_OUT_B)))
        sleep_us(500);
    sleep_us(700);
    s_core1_running = false;
    s_relays = false;
    gpio_put(PIN_BYPASS_RELAY, 0);
    s_led_until = 0;
}
void midi_io_reboot(void) { s_reboot = true; }          /* the page boots a fresh module */
bool midi_io_soft_wake(void) { return s_soft_wake; }
bool sim_io_reboot_requested(void) { return s_reboot; }
void sim_io_set_soft_wake(bool w) { s_soft_wake = w; }
void sim_io_power_cut(void) { s_core1_running = false; s_relays = false; s_led_until = 0; }
bool sim_io_relays(void) { return s_relays; }

/* ---- page-facing ---- */
bool sim_io_rx_at(int in, uint8_t b, uint64_t t) {
    if (in < 0 || in >= ME_NUM_INS) return false;
    if (s_rxh[in] - s_rxt[in] >= RXQ) return false;
    uint64_t now = time_us_64();
    if (t < now) t = now;
    rx_ent_t *e = &s_rx[in][s_rxh[in] & (RXQ - 1u)];
    e->not_before = t;
    e->b = b;
    s_rxh[in]++;
    uint64_t start = t > s_sched_end[in] ? t : s_sched_end[in];
    s_sched_end[in] = start + BYTE_US;
    return true;
}
uint64_t sim_io_rx_free_at(int in) {
    uint64_t now = time_us_64();
    return s_sched_end[in] > now ? s_sched_end[in] : now;
}
uint32_t sim_io_rx_pending(int in) { return s_rxh[in] - s_rxt[in]; }
void sim_io_rx_flush(int in) {
    s_rxt[in] = s_rxh[in];
    s_sched_end[in] = s_line_free[in];
}
uint32_t sim_io_rx_total(int in) { return s_rx_total[in]; }

int sim_io_out_read(int port, uint8_t *buf, double *times, int max) {
    int n = 0;
    while (n < max && s_capt[port] != s_caph[port]) {
        uint32_t i = s_capt[port] & (CAPQ - 1u);
        buf[n] = s_cap[port][i];
        times[n] = s_cap_t[port][i];
        s_capt[port]++;
        n++;
    }
    return n;
}
uint32_t sim_io_out_total(int port) { return s_cap_total[port]; }
bool sim_io_led(void) { return time_us_64() < s_led_until; }

/* ---- core1 ---- */
static void deliver(int in, uint64_t done) {
    rx_ent_t *e = &s_rx[in][s_rxt[in] & (RXQ - 1u)];
    uint8_t b = e->b;
    s_rxt[in]++;
    s_line_free[in] = done;
    s_rx_total[in]++;
    me_input_byte(&g_engine, in, b, done);
    me_mon_push(&g_engine, in, b, 0, done);
    s_last_rx_ms = (uint32_t)(done / 1000u);
    s_led_until = done + LED_HOLD_US;
}

static uint64_t rx_done_time(int in) {
    if (s_rxt[in] == s_rxh[in]) return UINT64_MAX;
    rx_ent_t *e = &s_rx[in][s_rxt[in] & (RXQ - 1u)];
    uint64_t start = e->not_before > s_line_free[in] ? e->not_before : s_line_free[in];
    return start + BYTE_US;
}

static void tx_service(uint64_t now) {
    for (int o = 0; o < ME_NUM_OUTS; o++) {
        for (int guard = 0; guard < 4; guard++) {
            /* shift register free -> FIFO byte starts on the wire */
            if (s_fifo_full[o] && s_shift_end[o] <= now) {
                /* back-to-back if the FIFO was loaded before the shift ended */
                uint64_t start = s_fifo_t[o] > s_shift_end[o] ? s_fifo_t[o] : s_shift_end[o];
                s_shift_end[o] = start + BYTE_US;
                s_fifo_full[o] = false;
                uint32_t i = s_caph[o] & (CAPQ - 1u);
                s_cap[o][i] = s_fifo[o];
                s_cap_t[o][i] = (double)start;
                s_caph[o]++;
                if (s_caph[o] - s_capt[o] > CAPQ) s_capt[o] = s_caph[o] - CAPQ;
                s_cap_total[o]++;
            }
            /* core1: "TXFE" -> write the next byte into the FIFO */
            if (!s_fifo_full[o]) {
                uint8_t b;
                if (me_out_pop(&g_engine, o, &b)) { s_fifo[o] = b; s_fifo_t[o] = now; s_fifo_full[o] = true; continue; }
            }
            break;
        }
    }
}

/* core1 stopped: the wires only. IN A reaches OUT A through the relay
 * contacts when they are dropped; nothing else moves. */
static void passive_until(uint64_t t) {
    for (int in = 0; in < ME_NUM_INS; in++) {
        uint64_t d;
        while ((d = rx_done_time(in)) <= t) {
            uint8_t b = s_rx[in][s_rxt[in] & (RXQ - 1u)].b;
            s_rxt[in]++;
            s_line_free[in] = d;
            s_rx_total[in]++;
            if (in == ME_IN_A && !s_relays) {
                uint32_t i = s_caph[ME_OUT_A] & (CAPQ - 1u);
                s_cap[ME_OUT_A][i] = b;
                s_cap_t[ME_OUT_A][i] = (double)(d - BYTE_US);
                s_caph[ME_OUT_A]++;
                if (s_caph[ME_OUT_A] - s_capt[ME_OUT_A] > CAPQ) s_capt[ME_OUT_A] = s_caph[ME_OUT_A] - CAPQ;
                s_cap_total[ME_OUT_A]++;
            }
        }
    }
}

static void core1_run_until(uint64_t t) {
    if (!s_core1_running) { passive_until(t); s_core1_now = t; return; }
    while (s_core1_now < t) {
        uint64_t step = s_core1_now + CORE1_STEP;
        if (step > t) step = t;
        s_core1_now = step;
        sim_set_time(step);
        /* receive, in time order across both inputs */
        for (;;) {
            uint64_t da = rx_done_time(ME_IN_A), db = rx_done_time(ME_IN_B);
            int in = da <= db ? ME_IN_A : ME_IN_B;
            uint64_t d = da <= db ? da : db;
            if (d > step) break;
            deliver(in, d);
        }
        me_service(&g_engine, step);
        tx_service(step);
    }
}

/* Advance simulated time to t: core1 runs throughout, timer IRQs fire on
 * time. core0's loop is driven by the caller (sim_main.c). */
void sim_advance_to(uint64_t t) {
    uint64_t now = time_us_64();
    while (now < t) {
        uint64_t step = t;
        uint64_t nt = sim_hal_next_timer();
        if (nt > now && nt < step) step = nt;
        core1_run_until(step);
        sim_set_time(step);
        sim_hal_run_timers(step);
        now = step;
    }
}
