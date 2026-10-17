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

    /* generator */
    uint8_t  gen_channel;    /* 1..16 */
    uint8_t  cc_number;
    uint8_t  cc_value;
    uint8_t  program;
    uint16_t bpm;            /* 30..300 */
} settings_t;

extern settings_t g_settings;

void settings_defaults(settings_t *s);

#endif /* MIDIUM_SETTINGS_H */
