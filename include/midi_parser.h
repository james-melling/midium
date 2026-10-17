/*
 * midi_parser.h - full MIDI 1.0 stream parser for display / logging (core0).
 *
 * Handles running status, realtime bytes interleaved anywhere (including
 * inside sysex and mid-message), system common clearing running status,
 * sysex of any length (first MIDI_SX_STORE bytes kept + total length) and
 * sysex terminated by a status byte other than F7. Portable C.
 */
#ifndef MIDIUM_MIDI_PARSER_H
#define MIDIUM_MIDI_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define MIDI_SX_STORE 32u   /* bytes of sysex kept per message (incl. F0) */

typedef enum {
    MT_NOTE_OFF = 0,
    MT_NOTE_ON,
    MT_POLY_AT,
    MT_CC,
    MT_PROGRAM,
    MT_CHAN_AT,
    MT_PITCH_BEND,
    MT_SYSEX,
    MT_MTC_QF,
    MT_SONG_POS,
    MT_SONG_SEL,
    MT_TUNE_REQ,
    MT_CLOCK,
    MT_START,
    MT_CONTINUE,
    MT_STOP,
    MT_ACTIVE_SENSE,
    MT_RESET,
    MT_UNDEFINED,     /* F4 F5 F9 FD, stray F7 */
    MT_ERROR,         /* synthesised: UART error / monitor gap */
    MT_COUNT
} midi_type_t;

/* flags */
#define MSG_F_SX_TRUNC   0x01u  /* sysex longer than MIDI_SX_STORE */
#define MSG_F_SX_UNTERM  0x02u  /* sysex ended by a status byte other than F7 */

/* MT_ERROR codes (in d1) */
#define MSG_ERR_FRAMING  1u
#define MSG_ERR_BREAK    2u
#define MSG_ERR_OVERRUN  3u
#define MSG_ERR_GAP      4u     /* monitor ring overflow: bytes not shown */

typedef struct {
    uint64_t t_us;          /* time of the first byte of the message */
    uint8_t  type;          /* midi_type_t */
    uint8_t  status;        /* raw status byte */
    uint8_t  d1, d2;
    uint8_t  flags;
    uint16_t sx_len;        /* total sysex length incl. F0 (and F7 if present) */
    uint8_t  sx[MIDI_SX_STORE];
} midi_msg_t;

typedef void (*midi_msg_cb)(const midi_msg_t *msg, void *ctx);

typedef struct {
    uint8_t  running;       /* channel voice running status */
    uint8_t  cur;           /* status of message being assembled, 0 = none */
    uint8_t  need, have;
    uint8_t  d[2];
    uint64_t t_start;
    bool     in_sysex;
    midi_msg_t sx;          /* sysex under construction */
    uint32_t stray_data;    /* data bytes with no status */
    midi_msg_cb cb;
    void    *ctx;
} midi_parser_t;

void midi_parser_init(midi_parser_t *p, midi_msg_cb cb, void *ctx);
void midi_parser_feed(midi_parser_t *p, uint8_t b, uint64_t t_us);
/* Forget partial state (e.g. after bytes were lost). Running status kept
 * cleared too, so following data bytes count as stray until a status. */
void midi_parser_reset(midi_parser_t *p);

#endif /* MIDIUM_MIDI_PARSER_H */
