/*
 * debug_log.c - decoded-message log over USB CDC. See debug_log.h.
 */
#include <stdarg.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "debug_log.h"
#include "midi_decode.h"
#include "settings.h"

void dbg_init(void) {
    stdio_init_all();
}

void dbg_printf(const char *fmt, ...) {
    if (!stdio_usb_connected()) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void dbg_log_msg(const midi_msg_t *m) {
    if (!g_settings.usb_log || !stdio_usb_connected()) return;
    if (g_settings.hide_realtime && midi_is_noisy_realtime(m)) return;
    char t[16], txt[MIDI_TEXT_MAX];
    decode_opts_t o = { g_settings.note_c3 };
    midi_format_time(m->t_us, t, sizeof t);
    midi_decode(m, &o, txt, sizeof txt);
    printf("%s  %s", t, txt);
    if (m->type == MT_SYSEX) {
        unsigned n = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
        printf("  [");
        for (unsigned i = 0; i < n; i++) printf(i ? " %02X" : "%02X", m->sx[i]);
        printf("%s]", (m->flags & MSG_F_SX_TRUNC) ? " ..." : "");
    }
    printf("\n");
}
