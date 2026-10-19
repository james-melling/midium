/*
 * mtc.c - MIDI Time Code reader. See mtc.h.
 */
#include <stdio.h>
#include <string.h>
#include "mtc.h"

uint8_t mtc_fps(uint8_t rate) {
    static const uint8_t F[4] = { 24, 25, 30, 30 };
    return F[rate & 3u];
}

const char *mtc_rate_name(uint8_t rate) {
    static const char *const N[4] = { "24", "25", "29.97df", "30" };
    return N[rate & 3u];
}

/* Drop-frame: frames 0 and 1 do not exist at the start of a minute unless
 * the minute is a multiple of 10. */
static inline bool dropped(const mtc_tc_t *t, uint8_t rate) {
    return rate == MTC_RATE_2997 && t->s == 0 && t->f < 2 && (t->m % 10u) != 0;
}

bool mtc_tc_valid(const mtc_tc_t *t, uint8_t rate) {
    return t->h < 24 && t->m < 60 && t->s < 60 && t->f < mtc_fps(rate) && !dropped(t, rate);
}

void mtc_tc_inc(mtc_tc_t *t, uint8_t rate) {
    if (++t->f < mtc_fps(rate)) return;
    t->f = 0;
    if (++t->s >= 60) {
        t->s = 0;
        if (++t->m >= 60) {
            t->m = 0;
            if (++t->h >= 24) t->h = 0;
        }
    }
    if (dropped(t, rate)) t->f = 2;
}

void mtc_tc_dec(mtc_tc_t *t, uint8_t rate) {
    bool at_start = t->f == 0 || (rate == MTC_RATE_2997 && t->s == 0 && t->f == 2 && (t->m % 10u) != 0);
    if (!at_start) { t->f--; return; }
    t->f = (uint8_t)(mtc_fps(rate) - 1u);
    if (t->s > 0) { t->s--; return; }
    t->s = 59;
    if (t->m > 0) { t->m--; return; }
    t->m = 59;
    t->h = t->h > 0 ? (uint8_t)(t->h - 1u) : 23u;
}

void mtc_tc_str(const mtc_tc_t *t, uint8_t rate, char *buf, size_t n) {
    snprintf(buf, n, "%02u:%02u:%02u%c%02u", (unsigned)t->h, (unsigned)t->m, (unsigned)t->s,
             rate == MTC_RATE_2997 ? ';' : ':', (unsigned)t->f);
}

void mtc_init(mtc_t *m) {
    memset(m, 0, sizeof *m);
    m->last_piece = -1;
}

static void quarter_step(mtc_t *m, int dir) {
    if (dir > 0) {
        if (++m->q >= 4) { m->q = 0; mtc_tc_inc(&m->tc, m->rate); }
    } else {
        if (m->q == 0) { m->q = 3; mtc_tc_dec(&m->tc, m->rate); }
        else m->q--;
    }
}

void mtc_quarter_frame(mtc_t *m, uint8_t d1, uint64_t t_us) {
    int piece = (d1 >> 4) & 7, val = d1 & 0x0F;
    int d = 0;
    if (m->last_piece >= 0) {
        if (piece == ((m->last_piece + 1) & 7)) d = +1;
        else if (piece == ((m->last_piece - 1) & 7)) d = -1;
    }
    m->qf_count++;
    m->last_qf_us = t_us ? t_us : 1u;
    if (d == 0 || (m->dir != 0 && d != m->dir)) {
        /* first piece, a gap, or a change of direction: wait for a new set */
        if (m->last_piece >= 0) m->breaks++;
        m->mask = 0;
        m->qf_sync = false;
        if (d == 0) m->dir = 0;     /* the next piece sets the direction afresh */
    }
    m->last_piece = (int8_t)piece;
    if (d != 0) m->dir = (int8_t)d;

    /* A set starts at piece 0 going forward or piece 7 in reverse. With no
     * direction known yet, either may be a start. */
    if ((piece == 0 && d >= 0) || (piece == 7 && d <= 0)) m->mask = 0;
    m->nib[piece] = (uint8_t)val;
    m->mask |= (uint8_t)(1u << piece);

    if (m->qf_sync && d != 0) quarter_step(m, d);

    bool done = m->mask == 0xFF && ((d > 0 && piece == 7) || (d < 0 && piece == 0));
    if (!done) return;
    m->mask = 0;
    mtc_tc_t t = {
        .f = (uint8_t)(m->nib[0] | (m->nib[1] & 1u) << 4),
        .s = (uint8_t)(m->nib[2] | (m->nib[3] & 3u) << 4),
        .m = (uint8_t)(m->nib[4] | (m->nib[5] & 3u) << 4),
        .h = (uint8_t)(m->nib[6] | (m->nib[7] & 1u) << 4),
    };
    uint8_t rate = (uint8_t)((m->nib[7] >> 1) & 3u);
    if (!mtc_tc_valid(&t, rate)) { m->breaks++; m->qf_sync = false; return; }
    /* The set describes the time it started: 2 frames ago (forward) or
     * 2 frames ahead (reverse). */
    if (d > 0) { mtc_tc_inc(&t, rate); mtc_tc_inc(&t, rate); }
    else       { mtc_tc_dec(&t, rate); mtc_tc_dec(&t, rate); }
    m->tc = t;
    m->rate = rate;
    m->q = 0;
    m->valid = true;
    m->qf_sync = true;
}

bool mtc_full_frame(mtc_t *m, const uint8_t *sx, unsigned len, uint64_t t_us) {
    if (len < 10 || sx[0] != 0xF0 || sx[1] != 0x7F || sx[3] != 0x01 || sx[4] != 0x01 || sx[9] != 0xF7)
        return false;
    mtc_tc_t t = { (uint8_t)(sx[5] & 0x1Fu), sx[6], sx[7], sx[8] };
    uint8_t rate = (uint8_t)((sx[5] >> 5) & 3u);
    m->full_count++;
    if (!mtc_tc_valid(&t, rate)) { m->breaks++; return true; }
    m->tc = t;
    m->rate = rate;
    m->q = 0;
    m->valid = true;
    m->qf_sync = false;
    m->mask = 0;
    m->last_piece = -1;
    m->dir = 0;
    m->last_full_us = t_us ? t_us : 1u;
    return true;
}
