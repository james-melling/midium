/*
 * generator.c - test MIDI generator. See generator.h.
 *
 * Non-realtime messages are queued whole and core1 merges them at the next
 * input message boundary. Realtime bytes (clock, start/stop/continue) bypass
 * that rule and are sent by core1 immediately.
 *
 * Clock: a repeating timer on core0 fires every 60 / (BPM x 24) s. The SDK
 * timer has 1 us resolution, so the period is kept in nanoseconds and the
 * remainder carried from tick to tick: long-term tempo is exact and per-tick
 * error is < 1 us (plus up to one MIDI byte time of queueing on core1).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "generator.h"
#include "midi_io.h"
#include "midi_defs.h"
#include "midi_parser.h"
#include "midi_decode.h"
#include "msc_decode.h"
#include "settings.h"

static char     s_last[MIDI_TEXT_MAX + 2];
static uint64_t s_last_t;

/* note test */
static bool     s_nt_pending;
static uint8_t  s_nt_ch, s_nt_note;
static uint64_t s_nt_off_at;

/* note sweep */
#define SWEEP_FIRST 48   /* C3 (C4 = 60) */
#define SWEEP_LAST  72   /* C5 */
#define SWEEP_STEP_US 120000u
static bool     s_ns_on;
static int      s_ns_note;      /* note currently sounding, -1 none */
static uint8_t  s_ns_ch;
static uint64_t s_ns_next;

/* pitch bend sweep: 0 -> +max (0.5 s) -> -max (1 s) -> 0 (0.5 s), 10 ms steps */
#define PB_STEP_US 10000u
#define PB_TOTAL_STEPS 200
static bool     s_pb_on;
static int      s_pb_step;
static uint8_t  s_pb_ch;
static uint64_t s_pb_next;
static int      s_pb_last = 99999;

/* clock */
static repeating_timer_t s_clk_timer;
static volatile bool     s_clk_on;
static volatile uint32_t s_clk_period_ns;
static uint32_t          s_clk_acc_ns;

/* panic */
static int s_panic_ch = -1;

static void set_last_from_bytes(const uint8_t *msg, uint8_t len);

static bool send(const uint8_t *msg, uint8_t len) {
    if (!me_gen_push(&g_engine, msg, len)) {
        snprintf(s_last, sizeof s_last, "QUEUE FULL");
        s_last_t = time_us_64();
        return false;
    }
    set_last_from_bytes(msg, len);
    return true;
}

static bool send3(uint8_t s, uint8_t a, uint8_t b) {
    uint8_t m[3] = { s, a, b };
    return send(m, 3);
}

static bool send2(uint8_t s, uint8_t a) {
    uint8_t m[2] = { s, a };
    return send(m, 2);
}

/* Decode what we sent for the UI footer. */
static midi_msg_t s_dec_msg;
static bool s_dec_ok;
static void dec_cb(const midi_msg_t *m, void *ctx) { (void)ctx; s_dec_msg = *m; s_dec_ok = true; }

static void set_last_from_bytes(const uint8_t *msg, uint8_t len) {
    static midi_parser_t p;
    midi_parser_init(&p, dec_cb, NULL);
    s_dec_ok = false;
    for (uint8_t i = 0; i < len; i++) midi_parser_feed(&p, msg[i], 0);
    if (s_dec_ok) {
        decode_opts_t o = { .note_c3 = g_settings.note_c3, .width = MIDI_TEXT_COLS - 1 };
        s_last[0] = '>';
        midi_decode(&s_dec_msg, &o, s_last + 1, sizeof s_last - 1);
    }
    s_last_t = time_us_64();
}

static uint8_t chan(void) {
    uint8_t c = g_settings.gen_channel;
    if (c < 1 || c > 16) c = 1;
    return (uint8_t)(c - 1u);
}

void gen_init(void) {
    s_last[0] = 0;
    s_ns_note = -1;
    gen_clock_bpm_changed();
}

/* ---- note test ---- */
void gen_note_test(void) {
    if (s_nt_pending) send3((uint8_t)(MIDI_NOTE_OFF | s_nt_ch), s_nt_note, 0);
    s_nt_ch = chan();
    s_nt_note = 60;
    if (send3((uint8_t)(MIDI_NOTE_ON | s_nt_ch), s_nt_note, 100)) {
        s_nt_pending = true;
        s_nt_off_at = time_us_64() + 250000u;
    }
}

/* ---- note sweep ---- */
void gen_note_sweep_toggle(void) {
    if (s_ns_on) {
        s_ns_on = false;
        if (s_ns_note >= 0) send3((uint8_t)(MIDI_NOTE_OFF | s_ns_ch), (uint8_t)s_ns_note, 0);
        s_ns_note = -1;
        return;
    }
    s_ns_on = true;
    s_ns_ch = chan();
    s_ns_note = -1;
    s_ns_next = time_us_64();
}
bool gen_note_sweep_active(void) { return s_ns_on; }

static void note_sweep_task(uint64_t now) {
    if (!s_ns_on || now < s_ns_next) return;
    if (s_ns_note >= 0) {
        if (!send3((uint8_t)(MIDI_NOTE_OFF | s_ns_ch), (uint8_t)s_ns_note, 0)) return;
    }
    int next = s_ns_note < 0 ? SWEEP_FIRST : s_ns_note + 1;
    if (next > SWEEP_LAST) {
        s_ns_on = false;
        s_ns_note = -1;
        return;
    }
    s_ns_note = -1;
    if (send3((uint8_t)(MIDI_NOTE_ON | s_ns_ch), (uint8_t)next, 100)) s_ns_note = next;
    s_ns_next += SWEEP_STEP_US;
    if (s_ns_next < now) s_ns_next = now + SWEEP_STEP_US;
}

/* ---- CC / PC ---- */
void gen_send_cc(void) {
    send3((uint8_t)(MIDI_CC | chan()), (uint8_t)(g_settings.cc_number & 0x7F),
          (uint8_t)(g_settings.cc_value & 0x7F));
}

void gen_send_pc(void) {
    send2((uint8_t)(MIDI_PROGRAM | chan()), (uint8_t)(g_settings.program & 0x7F));
}

/* ---- pitch bend sweep ---- */
static int pb_value(int step) {
    /* triangle: 0..50 up to +8191, 50..150 down to -8192, 150..200 back to 0 */
    if (step <= 50)  return (8191 * step) / 50;
    if (step <= 150) return 8191 - ((8191 + 8192) * (step - 50)) / 100;
    return -8192 + (8192 * (step - 150)) / 50;
}

void gen_pb_sweep_toggle(void) {
    if (s_pb_on) {
        s_pb_on = false;
        send3((uint8_t)(MIDI_PITCH_BEND | s_pb_ch), 0x00, 0x40);   /* centre */
        return;
    }
    s_pb_on = true;
    s_pb_ch = chan();
    s_pb_step = 0;
    s_pb_last = 99999;
    s_pb_next = time_us_64();
}
bool gen_pb_sweep_active(void) { return s_pb_on; }

static void pb_task(uint64_t now) {
    if (!s_pb_on || now < s_pb_next) return;
    int v = pb_value(s_pb_step);
    if (v != s_pb_last) {
        unsigned raw = (unsigned)(v + 8192);
        if (!send3((uint8_t)(MIDI_PITCH_BEND | s_pb_ch), (uint8_t)(raw & 0x7F), (uint8_t)(raw >> 7)))
            return;
        s_pb_last = v;
    }
    if (++s_pb_step > PB_TOTAL_STEPS) { s_pb_on = false; return; }
    s_pb_next += PB_STEP_US;
    if (s_pb_next < now) s_pb_next = now + PB_STEP_US;
}

/* ---- realtime ---- */
static void rt_push(uint8_t b) {
    /* Both the clock timer IRQ and the main loop (both on core0) produce into
     * the realtime queue; masking IRQs here keeps it single-producer. */
    uint32_t irq = save_and_disable_interrupts();
    me_gen_rt_push(&g_engine, b);
    restore_interrupts(irq);
}

void gen_realtime(uint8_t b) {
    rt_push(b);
    set_last_from_bytes(&b, 1);
}

static bool clock_cb(repeating_timer_t *rt) {
    if (!s_clk_on) return false;
    me_gen_rt_push(&g_engine, MIDI_CLOCK);      /* IRQ context: sole producer now */
    s_clk_acc_ns += s_clk_period_ns;
    uint32_t us = s_clk_acc_ns / 1000u;
    s_clk_acc_ns -= us * 1000u;
    rt->delay_us = -(int64_t)us;                /* fixed-rate from last target */
    return true;
}

void gen_clock_bpm_changed(void) {
    uint32_t bpm = g_settings.bpm;
    if (bpm < 30) bpm = 30;
    if (bpm > 300) bpm = 300;
    /* 60 s / (bpm * 24 ppqn) in ns */
    s_clk_period_ns = (uint32_t)(60000000000ull / (bpm * 24u));
}

void gen_clock_toggle(void) {
    if (s_clk_on) {
        s_clk_on = false;
        cancel_repeating_timer(&s_clk_timer);
        snprintf(s_last, sizeof s_last, ">CLOCK OFF");
        s_last_t = time_us_64();
        return;
    }
    gen_clock_bpm_changed();
    s_clk_acc_ns = 0;
    s_clk_on = true;
    /* first tick now-ish, then the callback sets the precise period */
    add_repeating_timer_us(-1000, clock_cb, NULL, &s_clk_timer);
    snprintf(s_last, sizeof s_last, ">CLOCK %u BPM", (unsigned)g_settings.bpm);
    s_last_t = time_us_64();
}
bool gen_clock_running(void) { return s_clk_on; }

/* ---- show control ---- */
void gen_msc(uint8_t cmd) {
    const settings_t *st = &g_settings;
    msc_gen_t g = {
        .dev = st->msc_dev, .fmt = msc_gen_format(st->msc_fmt_idx), .cmd = cmd,
        .cue_int = st->msc_cue_int, .cue_dec = st->msc_cue_dec, .list = st->msc_list,
        .time_s = st->msc_time_s, .ctrl = st->msc_ctrl, .value = st->msc_value,
        .macro = st->msc_macro,
    };
    uint8_t m[ME_GEN_MAX_LEN];
    size_t len = msc_gen_build(&g, m, sizeof m);
    if (!len) {
        snprintf(s_last, sizeof s_last, cmd == MSC_LOAD ? "LOAD needs a cue" : "MSC build failed");
        s_last_t = time_us_64();
        return;
    }
    send(m, (uint8_t)len);
}

void gen_msc_send(void) { gen_msc(msc_gen_command(g_settings.msc_cmd_idx)); }

void gen_mmc(uint8_t cmd) {
    const uint8_t m[] = { MIDI_SYSEX, 0x7F, 0x7F, 0x06, cmd, MIDI_EOX };
    send(m, sizeof m);
}

/* ---- panic ---- */
void gen_panic(void) {
    s_ns_on = false;
    s_ns_note = -1;
    s_nt_pending = false;
    s_pb_on = false;
    s_panic_ch = 0;
}

static void panic_task(void) {
    while (s_panic_ch >= 0 && s_panic_ch < 16) {
        if (!send3((uint8_t)(MIDI_CC | s_panic_ch), 123, 0)) return;
        s_panic_ch++;
    }
    if (s_panic_ch >= 16) {
        s_panic_ch = -1;
        snprintf(s_last, sizeof s_last, ">ALL NOTES OFF 1-16");
        s_last_t = time_us_64();
    }
}

void gen_task(uint64_t now) {
    if (s_nt_pending && now >= s_nt_off_at) {
        if (send3((uint8_t)(MIDI_NOTE_OFF | s_nt_ch), s_nt_note, 0)) s_nt_pending = false;
    }
    note_sweep_task(now);
    pb_task(now);
    panic_task();
}

const char *gen_last_text(void) { return s_last; }
uint64_t gen_last_time_us(void) { return s_last_t; }
