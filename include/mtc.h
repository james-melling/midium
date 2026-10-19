/*
 * mtc.h - MIDI Time Code reader: rebuilds the current position from quarter
 * frame messages (F1 0n..7n) and full-frame messages
 * (F0 7F <dev> 01 01 hh mm ss ff F7). Portable C, unit tested.
 *
 * Quarter frames (MTC spec, MIDI 1.0 detailed spec 4.2):
 *  - Forward: pieces 0..7 in order, two frames per set. Reverse (tape
 *    rewinding): pieces 7..0. The direction is read from the piece order.
 *  - The time is valid only after a complete set of 8 pieces in one
 *    direction. Any gap or change of direction starts a new set.
 *  - The time in a set is the time at which the set started, so when the
 *    last piece arrives the position is 2 frames later: forward, +2 frames
 *    when piece 7 completes the set; reverse, -2 frames when piece 0 does.
 *  - Between complete sets the position moves on by a quarter frame per
 *    piece (4 pieces per frame), so the display changes every frame, and the
 *    next complete set lands on the same value (no jumps).
 *  - Rate (piece 7 bits 1-2, full-frame hour bits 5-6): 0 = 24, 1 = 25,
 *    2 = 29.97 drop-frame, 3 = 30 fps. Drop-frame counting skips frames 0
 *    and 1 at the start of every minute except minutes 0, 10, 20, 30, 40, 50.
 *  - A full-frame message (a locate) sets the position directly; quarter
 *    frames after it need a complete set again before the position moves.
 */
#ifndef MIDIUM_MTC_H
#define MIDIUM_MTC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MTC_RATE_24    0u
#define MTC_RATE_25    1u
#define MTC_RATE_2997  2u     /* 29.97 drop-frame */
#define MTC_RATE_30    3u

/* Running = a quarter frame within this long; after that "stopped", and
 * the header keeps the last value for MTC_HOLD_US. */
#define MTC_RUN_TIMEOUT_US 250000u
#define MTC_HOLD_US        2000000u

typedef struct {
    uint8_t h, m, s, f;
} mtc_tc_t;

typedef struct {
    /* quarter-frame assembly */
    uint8_t  nib[8];
    uint8_t  mask;         /* pieces received in the current set */
    int8_t   last_piece;   /* -1 = none */
    int8_t   dir;          /* +1 forward, -1 reverse, 0 unknown */
    /* position */
    bool     valid;        /* tc holds a real position */
    bool     qf_sync;      /* advancing per quarter frame (after a complete set) */
    mtc_tc_t tc;
    uint8_t  q;            /* quarter frame within tc.f, 0..3 */
    uint8_t  rate;         /* MTC_RATE_* */
    uint64_t last_qf_us;   /* 0 = never */
    uint64_t last_full_us; /* 0 = never */
    uint32_t qf_count, full_count, breaks;
} mtc_t;

uint8_t     mtc_fps(uint8_t rate);           /* 24, 25, 30, 30 */
const char *mtc_rate_name(uint8_t rate);     /* "24", "25", "29.97df", "30" */
bool        mtc_tc_valid(const mtc_tc_t *t, uint8_t rate);
void        mtc_tc_inc(mtc_tc_t *t, uint8_t rate);   /* one frame forward (wraps at 24 h) */
void        mtc_tc_dec(mtc_tc_t *t, uint8_t rate);   /* one frame back */
/* "hh:mm:ss:ff" (";" before ff for drop-frame). buf >= 12. */
void        mtc_tc_str(const mtc_tc_t *t, uint8_t rate, char *buf, size_t n);

void mtc_init(mtc_t *m);
void mtc_quarter_frame(mtc_t *m, uint8_t d1, uint64_t t_us);
/* Returns true if sx (len bytes stored, starting F0) is an MTC full frame
 * (and applies it). */
bool mtc_full_frame(mtc_t *m, const uint8_t *sx, unsigned len, uint64_t t_us);

static inline bool mtc_running(const mtc_t *m, uint64_t now_us) {
    return m->valid && m->qf_sync && m->last_qf_us && now_us - m->last_qf_us < MTC_RUN_TIMEOUT_US;
}
/* Last activity (quarter frame or full frame), 0 = never. */
static inline uint64_t mtc_last_us(const mtc_t *m) {
    return m->last_qf_us > m->last_full_us ? m->last_qf_us : m->last_full_us;
}

/* Frames since 00:00:00:00 (real frames: drop-frame labels that are
 * skipped are not counted). */
int32_t mtc_tc_frames(const mtc_tc_t *t, uint8_t rate);
/* a - b in frames, wrapped into [-12 h, +12 h) of frames. */
int32_t mtc_tc_diff(const mtc_tc_t *a, const mtc_tc_t *b, uint8_t rate);

/* Activity (quarter or full frame) within MTC_HOLD_US. */
static inline bool mtc_active(const mtc_t *m, uint64_t now_us) {
    uint64_t t = mtc_last_us(m);
    return t && now_us - t < MTC_HOLD_US;
}

/* ---- which input the display shows (two independent readers) ----------
 * Modes (settings mtc_src): MTC_SRC_AUTO, MTC_SRC_A, MTC_SRC_B.
 * Auto latches to the first input that becomes active and stays on it while
 * it is active (activity within MTC_HOLD_US = 2 s), so two feeds never make
 * the display alternate. When it has been silent for 2 s, it moves to the
 * other input if that one is active. Before anything is latched and with
 * nothing active, the input with the most recent activity is shown. */
#define MTC_SRC_AUTO 0u
#define MTC_SRC_A    1u
#define MTC_SRC_B    2u
typedef struct {
    int8_t cur;            /* latched input, -1 = none yet */
} mtc_sel_t;
void mtc_sel_init(mtc_sel_t *s);
/* in[0] = IN A, in[1] = IN B. Returns 0 / 1, or -1 (auto, never any MTC). */
int  mtc_select(mtc_sel_t *s, const mtc_t *const in[2], uint8_t mode, uint64_t now_us);

#endif /* MIDIUM_MTC_H */
