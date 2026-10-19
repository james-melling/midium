/*
 * mon_view.c - monitor history with freeze, and the scroll / pause logic.
 * See mon_view.h. Portable C.
 */
#include <string.h>
#include "mon_view.h"

void hist_clear(msg_hist_t *h) {
    h->seq = 0;
    h->paused = false;
    h->f_next = 0;
}

void hist_push(msg_hist_t *h, const midi_msg_t *m) {
    h->live[h->seq % HISTORY_LEN] = *m;
    h->seq++;
}

uint32_t hist_live_next(const msg_hist_t *h) { return h->seq; }
uint32_t hist_next(const msg_hist_t *h) { return h->paused ? h->f_next : h->seq; }
uint32_t hist_oldest(const msg_hist_t *h) {
    uint32_t n = hist_next(h);
    return n > HISTORY_LEN ? n - HISTORY_LEN : 0;
}

const midi_msg_t *hist_peek(const msg_hist_t *h, uint32_t seq) {
    if (seq >= hist_next(h) || seq < hist_oldest(h)) return NULL;
    return h->paused ? &h->frozen[seq % HISTORY_LEN] : &h->live[seq % HISTORY_LEN];
}

bool hist_get(const msg_hist_t *h, uint32_t seq, midi_msg_t *out) {
    const midi_msg_t *m = hist_peek(h, seq);
    if (!m) return false;
    *out = *m;
    return true;
}

void hist_freeze(msg_hist_t *h) {
    memcpy(h->frozen, h->live, sizeof h->frozen);
    h->f_next = h->seq;
    h->paused = true;
}

void hist_thaw(msg_hist_t *h) { h->paused = false; }

uint32_t hist_new_since(const msg_hist_t *h) { return h->paused ? h->seq - h->f_next : 0u; }

/* ------------------------------------------------------------------------ */
/* history rows above the pinned line (pinned mode) or all rows */
static uint32_t hist_rows(bool pinned, int rows) {
    int n = pinned ? rows - 1 : rows;
    return n > 0 ? (uint32_t)n : 1u;
}

/* keep the cursor on screen */
static void follow(mon_view_t *v, const msg_hist_t *h, bool pinned, int rows) {
    uint32_t next = hist_next(h), H = hist_rows(pinned, rows);
    if (pinned && v->cursor == next - 1u) return;              /* the pinned line */
    if (v->cursor > v->anchor) v->anchor = v->cursor;
    if (v->anchor - v->cursor >= H) v->anchor = v->cursor + H - 1u;
}

void mv_resume(mon_view_t *v, msg_hist_t *h) {
    v->paused = false;
    hist_thaw(h);
}

mv_result_t mv_up(mon_view_t *v, msg_hist_t *h, bool pinned, int rows, unsigned steps) {
    if (!v->paused) {
        if (hist_live_next(h) == 0) return MV_NONE;
        hist_freeze(h);
        uint32_t next = hist_next(h);
        v->paused = true;
        v->cursor = next - 1u;
        v->anchor = pinned ? (next >= 2 ? next - 2u : 0u) : next - 1u;
        return MV_PAUSED;
    }
    uint32_t oldest = hist_oldest(h);
    if (v->cursor <= oldest) return MV_TOP;
    if (steps == 0) steps = 1;
    v->cursor = v->cursor - oldest > steps ? v->cursor - steps : oldest;
    follow(v, h, pinned, rows);
    return MV_MOVED;
}

mv_result_t mv_down(mon_view_t *v, msg_hist_t *h, bool pinned, int rows) {
    if (!v->paused) return MV_NONE;
    uint32_t next = hist_next(h);
    if (v->cursor + 1u >= next) {                              /* past the bottom */
        mv_resume(v, h);
        return MV_RESUMED;
    }
    v->cursor++;
    follow(v, h, pinned, rows);
    return MV_MOVED;
}

void mv_layout(const mon_view_t *v, const msg_hist_t *h, bool pinned, int rows,
               uint32_t *seq, uint8_t *flags) {
    uint32_t next = hist_next(h), oldest = hist_oldest(h);
    for (int i = 0; i < rows; i++) { seq[i] = 0; flags[i] = MV_ROW_EMPTY; }
    if (next == 0 || rows <= 0) return;
    int hist_bottom = rows - 1;
    if (pinned) {
        seq[rows - 1] = next - 1u;
        flags[rows - 1] = (uint8_t)(MV_ROW_PIN | (v->paused && v->cursor == next - 1u ? MV_ROW_CURSOR : 0u));
        hist_bottom = rows - 2;
        if (next < 2) return;
    }
    uint32_t anchor = v->paused ? v->anchor : (pinned ? next - 2u : next - 1u);
    for (int i = hist_bottom, k = 0; i >= 0; i--, k++) {
        if (anchor < (uint32_t)k) break;
        uint32_t s = anchor - (uint32_t)k;
        if (s < oldest) break;
        seq[i] = s;
        flags[i] = (uint8_t)(v->paused && s == v->cursor ? MV_ROW_CURSOR : 0u);
    }
}

bool mv_selected(const mon_view_t *v, const msg_hist_t *h, uint32_t *seq) {
    uint32_t next = hist_next(h);
    if (next == 0) return false;
    *seq = v->paused ? v->cursor : next - 1u;
    return hist_peek(h, *seq) != NULL;
}
