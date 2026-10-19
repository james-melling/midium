/*
 * monitor.c - parse the input byte ring into a message history (core0).
 * One parser per input; every message carries its input (midi_msg_t.src).
 */
#include <string.h>
#include "monitor.h"
#include "midi_io.h"
#include "midi_decode.h"
#include "msc_decode.h"
#include "settings.h"
#include "debug_log.h"
#include "logger.h"
#include "mtc.h"
#include "config.h"
#include "mon_view.h"

static midi_parser_t s_parser[ME_NUM_INS];
static msg_hist_t    s_hist;              /* history + frozen copy (mon_view.h) */
static uint32_t      s_counts[MT_COUNT];
static uint32_t      s_total;
static uint32_t      s_total_src[ME_NUM_INS];
static bool          s_changed;

static mtc_t     s_mtc[ME_NUM_INS];       /* one independent reader per input */
static mtc_sel_t s_mtc_sel;

static void on_msg(const midi_msg_t *m, void *ctx) {
    (void)ctx;
    if (m->type < MT_COUNT) s_counts[m->type]++;
    s_total++;
    if (m->src < ME_NUM_INS) s_total_src[m->src]++;
    dbg_log_msg(m);
    /* Every message goes to the flash log and the MTC reader, unfiltered. */
    logger_msg(m);
    if (m->src < ME_NUM_INS) {
        if (m->type == MT_MTC_QF) mtc_quarter_frame(&s_mtc[m->src], m->d1, m->t_us);
        else if (m->type == MT_SYSEX) mtc_full_frame(&s_mtc[m->src], m->sx, m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE, m->t_us);
    }
    /* Display filters (Display > Filters) apply as messages arrive: at
     * 120 BPM the clock alone would flush a 128-entry history in under 3 s.
     * Hidden messages are still counted above. */
    if (!msg_filter_pass(&g_settings.filter, m)) return;
    hist_push(&s_hist, m);
    s_changed = true;
}

static void push_error(uint8_t src, uint8_t code, uint64_t t) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.t_us = t;
    m.src = src;
    m.type = MT_ERROR;
    m.d1 = code;
    on_msg(&m, NULL);
}

void monitor_init(void) {
    for (int i = 0; i < ME_NUM_INS; i++) {
        mtc_init(&s_mtc[i]);
        midi_parser_init(&s_parser[i], on_msg, NULL);
        s_parser[i].src = (uint8_t)i;
    }
    mtc_sel_init(&s_mtc_sel);
    monitor_clear();
}

void monitor_clear(void) {
    hist_clear(&s_hist);
    s_total = 0;
    memset(s_total_src, 0, sizeof s_total_src);
    memset(s_counts, 0, sizeof s_counts);
    for (int i = 0; i < ME_NUM_INS; i++) s_parser[i].stray_data = 0;
    s_changed = true;
}

bool monitor_poll(uint32_t max_bytes) {
    me_mon_entry_t e;
    while (max_bytes-- && me_mon_pop(&g_engine, &e)) {
        uint8_t src = e.src < ME_NUM_INS ? e.src : ME_IN_A;
        if (e.flags & ME_MON_F_GAP) {
            push_error(src, MSG_ERR_GAP, e.t_us);
            midi_parser_reset(&s_parser[src]);
        }
        if (e.flags & ME_MON_F_ERR) {
            uint8_t code = e.byte == ME_ERR_FRAMING ? MSG_ERR_FRAMING
                         : e.byte == ME_ERR_BREAK   ? MSG_ERR_BREAK
                         : MSG_ERR_OVERRUN;
            push_error(src, code, e.t_us);
            continue;
        }
        midi_parser_feed(&s_parser[src], e.byte, e.t_us);
    }
    bool c = s_changed;
    s_changed = false;
    return c;
}

uint32_t monitor_next_seq(void) { return hist_next(&s_hist); }
uint32_t monitor_oldest_seq(void) { return hist_oldest(&s_hist); }
bool monitor_get(uint32_t seq, midi_msg_t *out) { return hist_get(&s_hist, seq, out); }
msg_hist_t *monitor_hist(void) { return &s_hist; }
uint32_t monitor_new_since_pause(void) { return hist_new_since(&s_hist); }

uint32_t monitor_type_count(midi_type_t t) { return t < MT_COUNT ? s_counts[t] : 0; }
uint32_t monitor_total_msgs(void) { return s_total; }
uint32_t monitor_input_msgs(int in) { return (in >= 0 && in < ME_NUM_INS) ? s_total_src[in] : 0; }
uint32_t monitor_stray_bytes(void) { return s_parser[ME_IN_A].stray_data + s_parser[ME_IN_B].stray_data; }

const mtc_t *monitor_mtc(int in) { return (in >= 0 && in < ME_NUM_INS) ? &s_mtc[in] : NULL; }

int monitor_mtc_select(uint64_t now) {
    const mtc_t *in[2] = { &s_mtc[ME_IN_A], &s_mtc[ME_IN_B] };
    return mtc_select(&s_mtc_sel, in, g_settings.mtc_src, now);
}
