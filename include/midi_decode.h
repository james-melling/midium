/*
 * midi_decode.h - turn parsed MIDI messages into short human-readable text
 * (fits 21 columns, the OLED width, unless decode_opts_t.width says otherwise)
 * plus a colour class. Portable C.
 */
#ifndef MIDIUM_MIDI_DECODE_H
#define MIDIUM_MIDI_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"

#define MIDI_TEXT_MAX 27   /* 26 chars (TFT width) + NUL */
#define MIDI_TEXT_COLS 21  /* default line width (OLED) */

/* Colour classes (the TFT maps these to colours; OLED ignores them). */
typedef enum {
    CLS_NORMAL = 0,
    CLS_NOTE,      /* green   */
    CLS_CC,        /* cyan    */
    CLS_PC,        /* yellow  */
    CLS_SYSEX,     /* magenta: sysex, MSC, MMC, MTC full frame */
    CLS_REALTIME,  /* grey    */
    CLS_ERROR,     /* red     */
    CLS_HEADER,    /* UI header */
    CLS_DIM,       /* UI secondary text */
    CLS_COUNT
} msg_class_t;

typedef struct {
    bool note_c3;   /* false: C4 = 60 (default), true: C3 = 60 (Yamaha) */
    uint8_t width;  /* columns available; 0 = MIDI_TEXT_COLS. Only MSC adapts to it. */
} decode_opts_t;

/* Note name, e.g. "C4", "F#-1". buf >= 5 bytes. */
void midi_note_name(uint8_t note, bool c3, char *buf, size_t n);

/* Decode to text (<= width chars, default 21). Returns the colour class. */
msg_class_t midi_decode(const midi_msg_t *m, const decode_opts_t *o, char *buf, size_t n);

/* Short type label used by the Stats screen, e.g. "Note On". */
const char *midi_type_label(midi_type_t t);

/* True for clock and active sensing (the "hide realtime" filter). */
bool midi_is_noisy_realtime(const midi_msg_t *m);

/* mm:ss.mmm from microseconds since boot (wraps at 100 minutes in the mm
 * field width; minutes keep counting). buf >= 12 bytes. */
void midi_format_time(uint64_t t_us, char *buf, size_t n);

#endif /* MIDIUM_MIDI_DECODE_H */
