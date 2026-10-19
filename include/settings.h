/*
 * settings.h - user settings. RAM only in milestone 1.
 *
 * TODO(M2): persist to flash (versioned struct + CRC in a reserved sector,
 * written the same way as the log: core0 interrupts off, core1 never parked;
 * see src/log_flash_rp2350.c). Keep everything the user can change in this
 * one struct so that is a single save/load.
 * TODO(M4): filter / transform rules will live alongside the routing matrix.
 */
#ifndef MIDIUM_SETTINGS_H
#define MIDIUM_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_engine.h"
#include "msg_filter.h"

typedef struct {
    me_settings_t route;     /* routing matrix: sources per output (read by core1) */

    /* display */
    bool    pin_latest;      /* monitor: newest message always on the bottom row */
    bool    note_c3;         /* false: C4 = 60, true: C3 = 60 */
    uint8_t time_col;        /* monitor time column: TIMECOL_* */
    uint8_t brightness;      /* 10..100 % */
    bool    usb_log;         /* printf decoded messages over USB CDC */
    msg_filter_t filter;     /* what the monitor and the log browser show */

    /* generator */
    uint8_t  gen_channel;    /* 1..16 */
    uint8_t  cc_number;
    uint8_t  cc_value;
    uint8_t  program;
    uint16_t bpm;            /* 30..300 */

    /* MSC generator */
    uint8_t  msc_fmt_idx;    /* index into gen_msc_formats() */
    uint8_t  msc_dev;        /* device_ID 0-127 */
    uint8_t  msc_cmd_idx;    /* index into gen_msc_commands() */
    uint16_t msc_cue_int;    /* cue = int.dec; 0.0 = no cue */
    uint8_t  msc_cue_dec;    /* 0..99 */
    uint8_t  msc_list;       /* 0 = none */
    uint16_t msc_time_s;     /* TIMED_GO / SET fade time, seconds */
    uint16_t msc_ctrl;       /* SET control number (14-bit) */
    uint16_t msc_value;      /* SET value (14-bit) */
    uint8_t  msc_macro;      /* FIRE macro */
} settings_t;

#define TIMECOL_OFF    0
#define TIMECOL_UPTIME 1     /* mm:ss.mmm since boot */
#define TIMECOL_TOD    2     /* time of day hh:mm:ss.mmm (uptime if the clock is not set) */

extern settings_t g_settings;

void settings_defaults(settings_t *s);

/* ---- routing presets (inputs only; generator routing is left alone) ----
 *   0  "A->A, A->B (thru)"   default: IN A to both outputs, IN B monitored only
 *   1  "A+B merge -> both"
 *   2  "A->A, B->B (split)"
 *   3  "B->A, B->B"                                                           */
#define ROUTE_PRESET_COUNT 4
const char *route_preset_name(int idx);
/* Apply a preset. Routes are added before any are removed, so a message
 * that is routed both before and after the change is never dropped by a
 * half-written matrix (core1 latches routes per message start). */
void route_preset_apply(me_settings_t *r, int idx);
/* True if the input columns of r match the preset. */
bool route_preset_active(const me_settings_t *r, int idx);
/* Header summary for one output: input letters ("a", "b", "ab") or "-". */
void route_summary(const me_settings_t *r, int out, char *buf, unsigned n);

#endif /* MIDIUM_SETTINGS_H */
