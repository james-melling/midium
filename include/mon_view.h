/*
 * mon_view.h - the monitor's message history and its scroll / pause logic.
 * Portable C, unit tested (test/test_v6.c).
 *
 * History: a ring of the last HISTORY_LEN messages, addressed by absolute
 * sequence number. Pausing FREEZES a copy of it: while paused every read
 * (the view) comes from the copy, so the whole screen, including the pinned
 * newest line, stays exactly as it was however fast new messages arrive;
 * new messages keep going into the live ring (and the log), counted for the
 * header ("PAUSED +1532"). Nothing the paused view shows can be
 * overwritten, so scrolling never jumps.
 *
 * View: the message rows of the monitor, newest at the bottom.
 *   pinned mode (Display > Pin latest): the bottom row is the newest message
 *     (inverse bar); the rows above are the history before it.
 *   unpinned: all rows are history.
 * The cursor is a sequence number. In pinned mode cursor == view_next - 1
 * means the pinned line is selected.
 *   live:   UP pauses with the cursor on the newest message.
 *   paused: UP / DOWN move the cursor (the view scrolls to keep it visible);
 *           DOWN on the newest message, or LEFT, resumes live.
 */
#ifndef MIDIUM_MON_VIEW_H
#define MIDIUM_MON_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"
#include "config.h"

typedef struct {
    midi_msg_t live[HISTORY_LEN];
    midi_msg_t frozen[HISTORY_LEN];
    uint32_t   seq;              /* next live sequence number */
    bool       paused;
    uint32_t   f_next;           /* paused: next seq when frozen */
} msg_hist_t;

void     hist_clear(msg_hist_t *h);          /* also resumes */
void     hist_push(msg_hist_t *h, const midi_msg_t *m);
uint32_t hist_live_next(const msg_hist_t *h);
/* The view: the frozen copy while paused, else live. */
uint32_t hist_next(const msg_hist_t *h);     /* one past newest */
uint32_t hist_oldest(const msg_hist_t *h);
bool     hist_get(const msg_hist_t *h, uint32_t seq, midi_msg_t *out);
const midi_msg_t *hist_peek(const msg_hist_t *h, uint32_t seq);   /* NULL if gone */
void     hist_freeze(msg_hist_t *h);
void     hist_thaw(msg_hist_t *h);
/* Messages added since the freeze (0 when live). */
uint32_t hist_new_since(const msg_hist_t *h);

/* ---- scroll / pause state ---- */
typedef struct {
    bool     paused;
    uint32_t cursor;            /* selected seq (paused) */
    uint32_t anchor;            /* seq on the bottom history row (paused) */
} mon_view_t;

typedef enum {
    MV_NONE = 0,
    MV_MOVED,                   /* cursor moved */
    MV_PAUSED,                  /* this event paused the view */
    MV_RESUMED,                 /* back to live */
    MV_TOP,                     /* paused, already on the oldest message */
} mv_result_t;

/* Row flags from mv_layout */
#define MV_ROW_EMPTY  0x01u
#define MV_ROW_PIN    0x02u     /* the pinned newest line */
#define MV_ROW_CURSOR 0x04u     /* selected (paused) */

/* rows = message rows on screen (>= 2). steps = lines to move (held key
 * acceleration). Pausing / resuming freezes / thaws h. */
mv_result_t mv_up(mon_view_t *v, msg_hist_t *h, bool pinned, int rows, unsigned steps);
mv_result_t mv_down(mon_view_t *v, msg_hist_t *h, bool pinned, int rows);
void        mv_resume(mon_view_t *v, msg_hist_t *h);
/* Which message each screen row shows (row 0 = top): seq[i] and flags[i]. */
void        mv_layout(const mon_view_t *v, const msg_hist_t *h, bool pinned, int rows,
                      uint32_t *seq, uint8_t *flags);
/* The selected message (paused), or the newest (live). False if none. */
bool        mv_selected(const mon_view_t *v, const msg_hist_t *h, uint32_t *seq);

#endif /* MIDIUM_MON_VIEW_H */
