/*
 * settings.c - defaults. Flash persistence arrives in milestone 2.
 */
#include "settings.h"

settings_t g_settings;

void settings_defaults(settings_t *s) {
    s->route.thru[ME_OUT_A] = true;
    s->route.thru[ME_OUT_B] = true;
    s->route.gen[ME_OUT_A] = true;
    s->route.gen[ME_OUT_B] = true;

    s->hide_realtime = true;
    s->note_c3 = false;
    s->time_column = false;
    s->brightness = 80;
    s->usb_log = true;
    s->msc_only = false;
    s->msc_dev_filter = -1;

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
