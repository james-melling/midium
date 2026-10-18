/*
 * msc_decode.h - MIDI Show Control 1.1.1 (incl. two-phase commit) decoder,
 * text formatter and message builder. Portable C, no Pico dependencies.
 *
 *   F0 7F <device_ID> 02 <command_format> <command> <data> F7
 *
 * device_ID: 00-6F individual, 70-7E group 1-15, 7F all-call.
 * Field layouts follow MSC 1.1.1 section 5 exactly; see msc_decode.c.
 */
#ifndef MIDIUM_MSC_DECODE_H
#define MIDIUM_MSC_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"
#include "midi_decode.h"

#define MSC_SUB_ID       0x02u   /* universal realtime sub-ID #1 */
#define MSC_MAX_LEN      128u    /* spec: total message length limit */
#define MSC_FIELD_MAX    40u     /* chars kept per Q_number / Q_list / Q_path */
#define MSC_DEV_ALL      0x7Fu
#define MSC_FMT_ALL      0x7Fu

/* Command codes */
#define MSC_GO           0x01u
#define MSC_STOP         0x02u
#define MSC_RESUME       0x03u
#define MSC_TIMED_GO     0x04u
#define MSC_LOAD         0x05u
#define MSC_SET          0x06u
#define MSC_FIRE         0x07u
#define MSC_ALL_OFF      0x08u
#define MSC_RESTORE      0x09u
#define MSC_RESET        0x0Au
#define MSC_GO_OFF       0x0Bu
#define MSC_GO_JAM_CLOCK 0x10u
#define MSC_STANDBY_PLUS 0x11u
#define MSC_STANDBY_MINUS 0x12u
#define MSC_SEQUENCE_PLUS 0x13u
#define MSC_SEQUENCE_MINUS 0x14u
#define MSC_START_CLOCK  0x15u
#define MSC_STOP_CLOCK   0x16u
#define MSC_ZERO_CLOCK   0x17u
#define MSC_SET_CLOCK    0x18u
#define MSC_MTC_CHASE_ON 0x19u
#define MSC_MTC_CHASE_OFF 0x1Au
#define MSC_OPEN_CUE_LIST 0x1Bu
#define MSC_CLOSE_CUE_LIST 0x1Cu
#define MSC_OPEN_CUE_PATH 0x1Du
#define MSC_CLOSE_CUE_PATH 0x1Eu
#define MSC_STANDBY      0x20u   /* two-phase commit */
#define MSC_STANDING_BY  0x21u
#define MSC_GO_2PC       0x22u
#define MSC_COMPLETE     0x23u
#define MSC_CANCEL       0x24u
#define MSC_CANCELLED    0x25u
#define MSC_ABORT        0x26u

/* Warning bits (msc_msg_t.warn) */
#define MSC_W_TRUNC       (1u << 0)   /* longer than the monitor keeps: data cut */
#define MSC_W_UNTERM      (1u << 1)   /* ended by a status byte, not F7 */
#define MSC_W_TOO_LONG    (1u << 2)   /* over 128 bytes (spec limit) */
#define MSC_W_SHORT       (1u << 3)   /* format / command / fixed fields missing */
#define MSC_W_FMT_UNKNOWN (1u << 4)
#define MSC_W_CMD_UNKNOWN (1u << 5)
#define MSC_W_EXTENSION   (1u << 6)   /* command_format or command 00 */
#define MSC_W_EXTRA       (1u << 7)   /* data where the command defines none */
#define MSC_W_Q_MISSING   (1u << 8)   /* required Q_number / Q_list / Q_path absent */
#define MSC_W_Q_CHARS     (1u << 9)   /* cue field byte not 0-9 or '.' */
#define MSC_W_Q_DOTS      (1u << 10)  /* ".." (decimal points not separated) */
#define MSC_W_Q_ORDER     (1u << 11)  /* Q_list / Q_path sent without the field before */
#define MSC_W_Q_LONG      (1u << 12)  /* cue field longer than MSC_FIELD_MAX, clipped */
#define MSC_W_TIME        (1u << 13)  /* time field out of range / reserved bits / form */
#define MSC_W_CHECKSUM    (1u << 14)  /* 2PC checksum mismatch */
#define MSC_W_SEQ0        (1u << 15)  /* 2PC sequence number 0 (reserved) */
#define MSC_W_COUNT       16u

/* Time code type (hr bits 6-5) */
#define MSC_TC_24   0u
#define MSC_TC_25   1u
#define MSC_TC_30DF 2u
#define MSC_TC_30   3u

/* Standard Time Specification: hr mn sc fr (ff|st) */
typedef struct {
    bool    present;
    uint8_t type;          /* MSC_TC_* */
    uint8_t hours, minutes, seconds, frames;
    bool    negative;      /* fr bit 6 (g) */
    bool    colour_frame;  /* mn bit 6 (c) */
    bool    is_status;     /* fr bit 5 (i): last byte is status {st}, else subframes {ff} */
    uint8_t subframes;     /* 0-99 (1/100 frame) when !is_status */
    uint8_t status;        /* st byte when is_status: 0 e v d 0000 */
    uint8_t raw[5];
} msc_time_t;
#define MSC_ST_ESTIMATED 0x40u
#define MSC_ST_INVALID   0x20u
#define MSC_ST_FIELD     0x10u

typedef struct {
    uint16_t len;          /* total message length incl. F0 (and F7) */
    uint8_t  dev;
    bool     has_fmt, has_cmd;
    uint8_t  fmt, cmd;

    bool has_cue, has_list, has_path;
    char cue[MSC_FIELD_MAX + 1], list[MSC_FIELD_MAX + 1], path[MSC_FIELD_MAX + 1];

    msc_time_t time;       /* TIMED_GO, SET (optional), SET_CLOCK, STANDING_BY */

    bool     has_ctrl;     /* SET */
    uint16_t ctrl, value;  /* 14-bit each */
    bool     has_macro;    /* FIRE */
    uint8_t  macro;

    /* two-phase commit */
    bool     is_2pc;
    bool     has_chk, chk_checked, chk_ok, chk_alt;
    uint16_t chk_rx, chk_calc;  /* as cc1 | cc2 << 8 (masked 7F7F) */
    bool     has_seq;
    uint16_t seq;          /* 14-bit */
    bool     has_d;
    uint8_t  d[4];
    bool     has_status;
    uint16_t status;       /* (s1 * 4) + (s2 * 512) */

    uint32_t warn;         /* MSC_W_* */
} msc_msg_t;

/* True if sx (stored sysex incl. F0) is F0 7F <dev> 02 ... */
bool msc_is_msc(const uint8_t *sx, size_t n);

/* Decode. sx = stored bytes (from F0), n = stored count, total_len = full
 * message length (> n when the capture was truncated). Returns false if
 * this is not an MSC message. */
bool msc_decode(const uint8_t *sx, size_t n, size_t total_len, msc_msg_t *d);
bool msc_decode_midi(const midi_msg_t *m, msc_msg_t *d);

/* Names. NULL when the code is not defined by the spec. */
const char *msc_format_name(uint8_t fmt);     /* "Lighting (General Category)" */
const char *msc_format_abbr(uint8_t fmt);     /* "LTG", <= 4 chars */
const char *msc_command_name(uint8_t cmd);    /* "TIMED_GO" */
const char *msc_command_abbr(uint8_t cmd);    /* "TGO", <= 6 chars */
const char *msc_status_text(uint8_t cmd, uint8_t fmt, uint16_t status);
const char *msc_warn_text(uint32_t bit);
const char *msc_tc_rate_name(uint8_t type);   /* "24", "25", "30DF", "30" */
unsigned    msc_tc_fps(uint8_t type);

/* Device ID as "1", "G1" or "ALL" (decimal; groups 70-7E are G1-G15). */
void msc_device_short(uint8_t dev, char *buf, size_t n);
/* "01h (1)", "70h Group 1", "7Fh All-call" */
void msc_device_long(uint8_t dev, char *buf, size_t n);

/* "hh:mm:ss:ff" (+ ".sf" subframes when sub), "-" prefix when negative.
 * frames=false gives "hh:mm:ss". */
void msc_time_str(const msc_time_t *t, bool frames, bool sub, char *buf, size_t n);

/* One-line summary that fits width columns, e.g. "MSC LTG GO Q12.5 @1".
 * Drops the device, then the format, then path/frames, then truncates.
 * A trailing '!' means the decoder raised a warning. */
void msc_summary(const msc_msg_t *d, unsigned width, char *buf, size_t n);

/* Full-screen detail: calls cb once per line of at most cols chars.
 * Works for any sysex: MSC fields when it is MSC, else ID / length; raw
 * hex at the end in both cases. Returns the number of lines. */
typedef void (*detail_line_cb)(const char *text, msg_class_t cls, void *ctx);
unsigned sysex_detail(const midi_msg_t *m, unsigned cols, detail_line_cb cb, void *ctx);

/* 2PC checksum over bytes sx[0..end) (end excludes F7), checksum slots
 * sx[6..7] treated as zero. alt=false: little-endian byte pairs. */
uint16_t msc_checksum(const uint8_t *sx, size_t end, bool alt);

/* ---- builder (generator, tests) ---- */
typedef struct {
    uint8_t dev, fmt, cmd;
    const char *cue, *list, *path;  /* NULL or "" = not sent */
    const msc_time_t *time;         /* TIMED_GO, SET_CLOCK, STANDING_BY; optional for SET */
    uint16_t ctrl, value;           /* SET */
    uint8_t  macro;                 /* FIRE */
    uint16_t seq;                   /* 2PC */
    uint8_t  d[4];                  /* 2PC STANDBY / GO_2PC */
    uint16_t status;                /* 2PC CANCELLED / ABORT */
} msc_build_t;

void msc_time_encode(const msc_time_t *t, uint8_t out[5]);
/* Returns the message length, or 0 if it does not fit in n (or > 128). */
size_t msc_build(const msc_build_t *b, uint8_t *out, size_t n);

/* ---- generator front end (portable so it can be round-trip tested) ---- */
typedef struct {
    uint8_t  dev, fmt, cmd;
    uint16_t cue_int;     /* cue = cue_int.cue_dec; 0.0 = no cue */
    uint8_t  cue_dec;     /* 0 = no decimal part */
    uint8_t  list;        /* 0 = no Q_list (only sent with a cue) */
    uint16_t time_s;      /* TIMED_GO time; SET fade time if > 0 (25 fps, ff form) */
    uint16_t ctrl, value; /* SET */
    uint8_t  macro;       /* FIRE */
} msc_gen_t;

/* Formats offered by the generator: 7F ALL, the general categories, then
 * every specific format. Commands: GO STOP RESUME TIMED_GO LOAD SET FIRE
 * ALL_OFF RESTORE RESET GO_OFF. */
#define MSC_GEN_FORMAT_COUNT  56u
#define MSC_GEN_COMMAND_COUNT 11u
unsigned msc_gen_format_count(void);
uint8_t  msc_gen_format(unsigned idx);
unsigned msc_gen_command_count(void);
uint8_t  msc_gen_command(unsigned idx);
/* Cue text "12" / "12.5" (empty for 0.0). buf >= 10 bytes. */
void     msc_gen_cue_text(uint16_t cue_int, uint8_t cue_dec, char *buf, size_t n);
/* Build the message. Returns 0 if it would not be well formed (LOAD needs
 * a cue) or does not fit in n. */
size_t   msc_gen_build(const msc_gen_t *g, uint8_t *out, size_t n);

#endif /* MIDIUM_MSC_DECODE_H */
