/*
 * midi_defs.h - MIDI 1.0 constants and byte classification helpers shared by
 * the core1 thru/merge engine and the core0 parser. Header-only, portable.
 */
#ifndef MIDIUM_MIDI_DEFS_H
#define MIDIUM_MIDI_DEFS_H

#include <stdint.h>
#include <stdbool.h>

/* Channel voice (high nibble) */
#define MIDI_NOTE_OFF        0x80
#define MIDI_NOTE_ON         0x90
#define MIDI_POLY_AT         0xA0
#define MIDI_CC              0xB0
#define MIDI_PROGRAM         0xC0
#define MIDI_CHAN_AT         0xD0
#define MIDI_PITCH_BEND      0xE0

/* System common */
#define MIDI_SYSEX           0xF0
#define MIDI_MTC_QF          0xF1
#define MIDI_SONG_POS        0xF2
#define MIDI_SONG_SEL        0xF3
#define MIDI_UNDEF_F4        0xF4
#define MIDI_UNDEF_F5        0xF5
#define MIDI_TUNE_REQ        0xF6
#define MIDI_EOX             0xF7

/* System realtime */
#define MIDI_CLOCK           0xF8
#define MIDI_UNDEF_F9        0xF9
#define MIDI_START           0xFA
#define MIDI_CONTINUE        0xFB
#define MIDI_STOP            0xFC
#define MIDI_UNDEF_FD        0xFD
#define MIDI_ACTIVE_SENSE    0xFE
#define MIDI_RESET           0xFF

static inline bool midi_is_status(uint8_t b)   { return (b & 0x80u) != 0; }
static inline bool midi_is_realtime(uint8_t b) { return b >= 0xF8u; }
static inline bool midi_is_channel(uint8_t b)  { return b >= 0x80u && b < 0xF0u; }

/*
 * Number of data bytes that follow a (non-realtime, non-sysex) status byte.
 * Undefined system common bytes (F4, F5) and Tune Request take none.
 */
static inline uint8_t midi_data_len(uint8_t status) {
    switch (status & 0xF0u) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    default: break;
    }
    switch (status) {
    case MIDI_MTC_QF:   return 1;
    case MIDI_SONG_POS: return 2;
    case MIDI_SONG_SEL: return 1;
    default:            return 0;
    }
}

#endif /* MIDIUM_MIDI_DEFS_H */
