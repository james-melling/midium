/*
 * settings.c - defaults and routing presets. Settings persistence is still
 * to come (M2).
 */
#include <stdio.h>
#include "settings.h"

settings_t g_settings;

/* [preset][out][input] */
static const bool PRESETS[ROUTE_PRESET_COUNT][ME_NUM_OUTS][ME_NUM_INS] = {
    { { true,  false }, { true,  false } },   /* Thru:  IN A -> OUT A + OUT B */
    { { true,  true  }, { true,  true  } },   /* Merge: IN A + IN B -> both outs */
    { { true,  false }, { false, true  } },   /* Split: IN A -> OUT A, IN B -> OUT B */
    { { false, true  }, { false, true  } },   /* B to both: IN B -> OUT A + OUT B */
};

/* Short name, and the signal flow read left to right (IN -> OUT): 40-column
 * and 21-column forms. RA = the right-arrow glyph (display.h CH_RIGHT). */
#define RA "\x1A"
static const char *const PRESET_NAMES[ROUTE_PRESET_COUNT] = { "Thru", "Merge", "Split", "B to both" };
static const char *const PRESET_FLOW[ROUTE_PRESET_COUNT] = {
    "IN A " RA " OUT A + OUT B",
    "IN A + IN B " RA " both outs",
    "IN A " RA " OUT A, IN B " RA " OUT B",
    "IN B " RA " OUT A + OUT B",
};
static const char *const PRESET_FLOW_SHORT[ROUTE_PRESET_COUNT] = {
    "A" RA "A+B",
    "A+B" RA "both",
    "A" RA "A, B" RA "B",
    "B" RA "A+B",
};
#undef RA

const char *route_preset_name(int idx) {
    return (idx >= 0 && idx < ROUTE_PRESET_COUNT) ? PRESET_NAMES[idx] : "?";
}

const char *route_preset_flow(int idx, bool wide) {
    if (idx < 0 || idx >= ROUTE_PRESET_COUNT) return "?";
    return wide ? PRESET_FLOW[idx] : PRESET_FLOW_SHORT[idx];
}

void route_preset_apply(me_settings_t *r, int idx) {
    if (idx < 0 || idx >= ROUTE_PRESET_COUNT) return;
    for (int o = 0; o < ME_NUM_OUTS; o++)
        for (int i = 0; i < ME_NUM_INS; i++)
            if (PRESETS[idx][o][i]) r->src[o][i] = true;
    for (int o = 0; o < ME_NUM_OUTS; o++)
        for (int i = 0; i < ME_NUM_INS; i++)
            if (!PRESETS[idx][o][i]) r->src[o][i] = false;
}

bool route_preset_active(const me_settings_t *r, int idx) {
    if (idx < 0 || idx >= ROUTE_PRESET_COUNT) return false;
    for (int o = 0; o < ME_NUM_OUTS; o++)
        for (int i = 0; i < ME_NUM_INS; i++)
            if (r->src[o][i] != PRESETS[idx][o][i]) return false;
    return true;
}

void route_summary(const me_settings_t *r, int src, char *buf, unsigned n) {
    unsigned k = 0;
    if (n == 0) return;
    if (r->src[ME_OUT_A][src] && k + 1 < n) buf[k++] = 'A';
    if (r->src[ME_OUT_B][src] && k + 1 < n) buf[k++] = 'B';
    if (k == 0 && k + 1 < n) buf[k++] = '-';
    buf[k] = 0;
}

void route_summary_line(const me_settings_t *r, char *buf, unsigned n) {
    char a[4], b[4];
    if (n == 0) return;
    route_summary(r, ME_SRC_A, a, sizeof a);
    route_summary(r, ME_SRC_B, b, sizeof b);
    snprintf(buf, n, "A\x1A%-2s B\x1A%-2s", a, b);
}

void route_flow_text(const me_settings_t *r, int src, bool wide, char *buf, unsigned n) {
    static const char *const SRC[ME_NUM_SRCS] = { "IN A", "IN B", "GEN" };
    bool oa = r->src[ME_OUT_A][src], ob = r->src[ME_OUT_B][src];
    const char *to = oa && ob ? "OUT A, OUT B" : oa ? "OUT A" : ob ? "OUT B"
                   : src == ME_SRC_GEN ? (wide ? "not sent" : "off") : (wide ? "monitor only" : "monitored");
    if (n == 0) return;
    snprintf(buf, n, "%s \x1A %s", SRC[src], to);
}

void settings_defaults(settings_t *s) {
    route_preset_apply(&s->route, 0);
    s->route.src[ME_OUT_A][ME_SRC_GEN] = true;
    s->route.src[ME_OUT_B][ME_SRC_GEN] = true;

    s->pin_latest = true;
    s->note_c3 = false;
    s->time_col = TIMECOL_OFF;
    s->brightness = 80;
    s->usb_log = true;
    msg_filter_defaults(&s->filter);
    s->mtc_src = 0;          /* MTC_SRC_AUTO */

    s->usb_power_on = USBPO_CHARGE;
    s->auto_off_idx = 0;     /* Off */

    s->gen_channel = 1;
    s->cc_number = 7;
    s->cc_value = 100;
    s->program = 0;
    s->bpm = 120;

    s->msc_fmt_idx = 0;      /* 7F All-types (as M1) */
    s->msc_dev = 0x7F;       /* all-call (as M1) */
    s->msc_cmd_idx = 0;      /* GO */
    s->msc_cue_int = 1;
    s->msc_cue_dec = 0;
    s->msc_list = 0;
    s->msc_time_s = 5;
    s->msc_ctrl = 0;
    s->msc_value = 255;
    s->msc_macro = 1;
}
