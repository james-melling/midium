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
    uint8_t mtc_src;         /* MTC shown in the header / MTC display: MTC_SRC_* (mtc.h) */

    /* power */
    uint8_t usb_power_on;    /* USBPO_*: what plugging USB into a box that is off does */
    uint8_t auto_off_idx;    /* auto-off on battery: 0 Off, 15, 30, 60, 120 min (autooff_minutes) */

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

#define USBPO_CHARGE   0     /* charge only, stay off (default) */
#define USBPO_POWER_ON 1     /* power on (needs the VBUS -> controller ON strap) */

#define TIMECOL_OFF    0
#define TIMECOL_UPTIME 1     /* mm:ss.mmm since boot */
#define TIMECOL_TOD    2     /* time of day hh:mm:ss.mmm (uptime if the clock is not set) */

extern settings_t g_settings;

void settings_defaults(settings_t *s);

/* ---- routing presets (inputs only; generator routing is left alone) ----
 * Read as signal flow, IN -> OUT:
 *   0  Thru       IN A -> OUT A + OUT B   (default; IN B monitored only)
 *   1  Merge      IN A + IN B -> both outs
 *   2  Split      IN A -> OUT A, IN B -> OUT B
 *   3  B to both  IN B -> OUT A + OUT B                                     */
#define ROUTE_PRESET_COUNT 4
const char *route_preset_name(int idx);
/* The preset's signal flow, arrows as CH_RIGHT (0x1A): wide = the 40-column
 * form ("IN A > OUT A + OUT B"), else the 21-column one ("A>A+B"). */
const char *route_preset_flow(int idx, bool wide);
/* Apply a preset. Routes are added before any are removed, so a message
 * that is routed both before and after the change is never dropped by a
 * half-written matrix (core1 latches routes per message start). */
void route_preset_apply(me_settings_t *r, int idx);
/* True if the input columns of r match the preset. */
bool route_preset_active(const me_settings_t *r, int idx);
/* For one input (ME_SRC_A / _B / _GEN): the outputs it feeds, "AB", "A",
 * "B", or "-" for none. */
void route_summary(const me_settings_t *r, int src, char *buf, unsigned n);
/* Monitor header routing, per input, IN -> OUT, 9 cells fixed width:
 * "A>AB B>- " (> = CH_RIGHT 0x1A): IN A feeds both outputs, IN B none. */
void route_summary_line(const me_settings_t *r, char *buf, unsigned n);
/* One source's routing in words: "IN A > OUT A, OUT B", "IN B > monitor
 * only" (wide) / "IN B > monitored", "GEN > not sent" / "GEN > off". */
void route_flow_text(const me_settings_t *r, int src, bool wide, char *buf, unsigned n);

#endif /* MIDIUM_SETTINGS_H */
