/*
 * settings.h - user settings. RAM only in milestone 1.
 *
 * TODO(M2): persist to flash (versioned struct + CRC in a reserved sector,
 * written with core1 parked in RAM via multicore_lockout). Keep everything
 * the user can change in this one struct so that is a single save/load.
 * TODO(M4): routing rules (filter / transform) will live alongside.
 */
#ifndef MIDIUM_SETTINGS_H
#define MIDIUM_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_engine.h"

typedef struct {
    me_settings_t route;     /* thru A/B, generator to A/B (read by core1) */

    /* display */
    bool    hide_realtime;   /* hide clock + active sensing in the monitor */
    bool    note_c3;         /* false: C4 = 60, true: C3 = 60 */
    bool    time_column;     /* monitor shows mm:ss.mmm column */
    uint8_t brightness;      /* 10..100 % */
    bool    usb_log;         /* printf decoded messages over USB CDC */
    bool    msc_only;        /* monitor keeps only MSC (and error) messages */
    int16_t msc_dev_filter;  /* -1 = all, else MSC device_ID 0-127 (all-call 7F also passes) */

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

extern settings_t g_settings;

void settings_defaults(settings_t *s);

#endif /* MIDIUM_SETTINGS_H */
