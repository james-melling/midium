/*
 * monitor.c - parse the input byte ring into a message history (core0).
 */
#include <string.h>
#include "monitor.h"
#include "midi_io.h"
#include "midi_decode.h"
#include "settings.h"
#include "debug_log.h"
#include "config.h"

static midi_parser_t s_parser;
static midi_msg_t    s_hist[HISTORY_LEN];
static uint32_t      s_seq;               /* next sequence number */
static uint32_t      s_counts[MT_COUNT];
static uint32_t      s_total;
static bool          s_changed;

static void on_msg(const midi_msg_t *m, void *ctx) {
    (void)ctx;
    if (m->type < MT_COUNT) s_counts[m->type]++;
    s_total++;
    dbg_log_msg(m);
    /* "Hide realtime" filters at insertion: at 120 BPM the clock alone would
     * flush a 128-entry history in under 3 seconds. Still counted above. */
    if (g_settings.hide_realtime && midi_is_noisy_realtime(m)) return;
    s_hist[s_seq % HISTORY_LEN] = *m;
    s_seq++;
    s_changed = true;
}

static void push_error(uint8_t code, uint64_t t) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.t_us = t;
    m.type = MT_ERROR;
    m.d1 = code;
    on_msg(&m, NULL);
}

void monitor_init(void) {
    midi_parser_init(&s_parser, on_msg, NULL);
    monitor_clear();
}

void monitor_clear(void) {
    s_seq = 0;
    s_total = 0;
    memset(s_counts, 0, sizeof s_counts);
    s_parser.stray_data = 0;
    s_changed = true;
}

bool monitor_poll(uint32_t max_bytes) {
    me_mon_entry_t e;
    while (max_bytes-- && me_mon_pop(&g_engine, &e)) {
        if (e.flags & ME_MON_F_GAP) {
            push_error(MSG_ERR_GAP, e.t_us);
            midi_parser_reset(&s_parser);
        }
        if (e.flags & ME_MON_F_ERR) {
            uint8_t code = e.byte == ME_ERR_FRAMING ? MSG_ERR_FRAMING
                         : e.byte == ME_ERR_BREAK   ? MSG_ERR_BREAK
                         : MSG_ERR_OVERRUN;
            push_error(code, e.t_us);
            continue;
        }
        midi_parser_feed(&s_parser, e.byte, e.t_us);
    }
    bool c = s_changed;
    s_changed = false;
    return c;
}

uint32_t monitor_next_seq(void) { return s_seq; }
uint32_t monitor_oldest_seq(void) { return s_seq > HISTORY_LEN ? s_seq - HISTORY_LEN : 0; }

bool monitor_get(uint32_t seq, midi_msg_t *out) {
    if (seq >= s_seq || seq < monitor_oldest_seq()) return false;
    *out = s_hist[seq % HISTORY_LEN];
    return true;
}

uint32_t monitor_type_count(midi_type_t t) { return t < MT_COUNT ? s_counts[t] : 0; }
uint32_t monitor_total_msgs(void) { return s_total; }
uint32_t monitor_stray_bytes(void) { return s_parser.stray_data; }
