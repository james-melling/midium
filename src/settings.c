/*
 * settings.c - defaults and routing presets. Flash persistence arrives in
 * milestone 2.
 */
#include "settings.h"

settings_t g_settings;

/* [preset][out][input] */
static const bool PRESETS[ROUTE_PRESET_COUNT][ME_NUM_OUTS][ME_NUM_INS] = {
    { { true,  false }, { true,  false } },   /* A->A, A->B (thru) */
    { { true,  true  }, { true,  true  } },   /* A+B merge -> both */
    { { true,  false }, { false, true  } },   /* A->A, B->B (split) */
    { { false, true  }, { false, true  } },   /* B->A, B->B */
};

static const char *const PRESET_NAMES[ROUTE_PRESET_COUNT] = {
    "A->A, A->B (thru)",
    "A+B merge -> both",
    "A->A, B->B (split)",
    "B->A, B->B",
};

const char *route_preset_name(int idx) {
    return (idx >= 0 && idx < ROUTE_PRESET_COUNT) ? PRESET_NAMES[idx] : "?";
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

void route_summary(const me_settings_t *r, int out, char *buf, unsigned n) {
    unsigned k = 0;
    if (n == 0) return;
    if (r->src[out][ME_SRC_A] && k + 1 < n) buf[k++] = 'a';
    if (r->src[out][ME_SRC_B] && k + 1 < n) buf[k++] = 'b';
    if (k == 0 && k + 1 < n) buf[k++] = '-';
    buf[k] = 0;
}

void settings_defaults(settings_t *s) {
    route_preset_apply(&s->route, 0);
    s->route.src[ME_OUT_A][ME_SRC_GEN] = true;
    s->route.src[ME_OUT_B][ME_SRC_GEN] = true;

    s->hide_realtime = true;
    s->note_c3 = false;
    s->time_column = false;
    s->brightness = 80;
    s->usb_log = true;
    s->msc_only = false;
    s->msc_dev_filter = -1;
    s->show_inputs = SHOW_IN_BOTH;

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
