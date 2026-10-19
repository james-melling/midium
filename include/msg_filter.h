/*
 * msg_filter.h - what the monitor (and the log browser) shows: per message
 * type, per input, per MIDI channel, and the MSC device filter. Portable C,
 * unit tested.
 *
 * Filters only affect what is displayed. Stats and the flash log see every
 * message.
 */
#ifndef MIDIUM_MSG_FILTER_H
#define MIDIUM_MSG_FILTER_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"

/* Filter categories (bit numbers in msg_filter_t.types). */
typedef enum {
    FC_NOTE = 0,     /* note on / note off */
    FC_POLY_AT,
    FC_CC,
    FC_PROGRAM,
    FC_CHAN_AT,
    FC_PITCH_BEND,
    FC_SYSEX,        /* sysex other than MSC, MMC and MTC full frame */
    FC_MSC,
    FC_MMC,          /* MMC commands (sub-ID 06) and responses (07) */
    FC_MTC_QF,
    FC_MTC_FULL,
    FC_SONG,         /* song position / song select */
    FC_TUNE,
    FC_CLOCK,
    FC_TRANSPORT,    /* start / stop / continue */
    FC_ACTIVE_SENSE,
    FC_RESET,
    FC_UNDEFINED,    /* F4 F5 F9 FD, stray F7 */
    FC_ERROR,        /* UART errors, monitor gaps */
    FC_COUNT
} filter_cat_t;

#define FC_BIT(c)    (1u << (c))
#define FC_ALL_MASK  ((1u << FC_COUNT) - 1u)

/* Show-inputs values */
#define SHOW_IN_BOTH 0
#define SHOW_IN_A    1
#define SHOW_IN_B    2

typedef struct {
    uint32_t types;      /* FC_BIT(cat) set = shown */
    int8_t   channel;    /* -1 = all, else 0..15: channel messages on other channels hidden */
    uint8_t  inputs;     /* SHOW_IN_* */
    int16_t  msc_dev;    /* -1 = all, else MSC device_ID (all-call 7F always passes) */
} msg_filter_t;

/* Quick presets. They set every type except Errors, which keeps its own
 * toggle. */
typedef enum {
    FP_ALL = 0,
    FP_SHOW_CONTROL,     /* MSC, MMC, MTC full frame, start/stop/continue, PC */
    FP_NO_REALTIME,      /* everything but clock, active sensing, MTC quarter frames (default) */
    FP_NONE,
    FP_COUNT
} filter_preset_t;

filter_cat_t msg_filter_cat(const midi_msg_t *m);
const char  *msg_filter_cat_label(filter_cat_t c);   /* menu label, <= 15 chars */
const char  *msg_filter_preset_name(filter_preset_t p);
void         msg_filter_defaults(msg_filter_t *f);   /* FP_NO_REALTIME, errors on, all channels / inputs / devices */
void         msg_filter_apply_preset(msg_filter_t *f, filter_preset_t p);
bool         msg_filter_preset_active(const msg_filter_t *f, filter_preset_t p);
bool         msg_filter_pass(const msg_filter_t *f, const midi_msg_t *m);
/* Type bits only (used for the USB debug log). */
bool         msg_filter_type_pass(const msg_filter_t *f, const midi_msg_t *m);

#endif /* MIDIUM_MSG_FILTER_H */
