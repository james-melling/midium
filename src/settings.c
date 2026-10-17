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

    s->gen_channel = 1;
    s->cc_number = 7;
    s->cc_value = 100;
    s->program = 0;
    s->bpm = 120;
}
