/*
 * midi_engine.c - byte-level MIDI thru + merge engine. See midi_engine.h.
 *
 * Portable C: no Pico SDK headers. On target, ME_RAMFUNC places the hot
 * functions in SRAM so flash (XIP) activity cannot stall the thru path.
 */
#include <string.h>
#include "midi_engine.h"
#include "midi_defs.h"

/* ------------------------------------------------------------------------ */
/* Output queues (core1-private, so plain indices)                           */
/* ------------------------------------------------------------------------ */

static inline uint32_t outq_fill(const me_out_t *o) { return o->qh - o->qt; }

static inline void outq_push(me_out_t *o, uint8_t b) {
    if (outq_fill(o) >= ME_OUTQ_SIZE) { o->drops++; return; }
    o->q[o->qh & (ME_OUTQ_SIZE - 1u)] = b;
    o->qh++;
}

static inline void outrt_push(me_out_t *o, uint8_t b) {
    if (o->rh - o->rt_t >= ME_OUT_RTQ_SIZE) { o->drops++; return; }
    o->rt[o->rh & (ME_OUT_RTQ_SIZE - 1u)] = b;
    o->rh++;
}

/*
 * Routing stage hook.
 *
 * TODO(M4): filter / transform rules (channel remap, per-type filters). In
 * milestone 1 this is a pure pass-through. Rules that rewrite a message need
 * the whole message, so when a rule is active on a port the M4 engine will
 * assemble the message first (one message of added latency on that port
 * only) and then emit the transformed bytes through this stage. Ports
 * without rules keep the byte-at-a-time path below.
 */
static inline bool me_route_byte(me_t *me, int port, uint8_t b) {
    (void)me; (void)port; (void)b;
    return true;
}

static inline void fwd(me_t *me, int port, uint8_t b) {
    if (me_route_byte(me, port, b)) outq_push(&me->out[port], b);
}

/* ------------------------------------------------------------------------ */

void me_init(me_t *me, me_settings_t *set) {
    memset(me, 0, sizeof(*me));
    me->set = set;
    spsc_init(&me->genq_idx);
    spsc_init(&me->rtq_idx);
    spsc_init(&me->mon_idx);
}

bool ME_RAMFUNC(me_at_boundary)(const me_t *me) {
    return me->forced || (!me->in_sysex && me->cur == 0);
}

/* Start of a new input message: latch thru state per output. */
static inline void start_msg(me_t *me) {
    me->forced = false;
    for (int p = 0; p < ME_NUM_OUTS; p++) {
        me->out[p].latched = me->set->thru[p];
        me->out[p].skip = false;
    }
}

void ME_RAMFUNC(me_input_byte)(me_t *me, uint8_t b, uint64_t now_us) {
    me->stats.bytes_in++;
    me->last_in_us = now_us;

    /* Realtime: forward immediately, ahead of anything queued. Does not touch
     * parser state, latches or running status. */
    if (midi_is_realtime(b)) {
        for (int p = 0; p < ME_NUM_OUTS; p++)
            if (me->set->thru[p] && me_route_byte(me, p, b))
                outrt_push(&me->out[p], b);
        return;
    }

    if (midi_is_status(b)) {
        if (b == MIDI_EOX && me->in_sysex) {
            /* End of the sysex in progress: belongs to the message latched at F0. */
            me->in_sysex = false;
            me->forced = false;
            for (int p = 0; p < ME_NUM_OUTS; p++) {
                me_out_t *o = &me->out[p];
                if (o->latched && !o->skip) { fwd(me, p, b); o->last_status = 0; }
            }
            return;
        }

        /* Any other status byte starts a new message (and implicitly ends an
         * unterminated sysex). */
        me->in_sysex = false;
        start_msg(me);
        if (b == MIDI_SYSEX) {
            me->in_sysex = true;
            me->running = 0;
            me->cur = 0;
        } else if (b >= 0xF0) {
            /* System common (incl. stray F7): clears running status. */
            me->running = 0;
            me->cur = b;
            me->need = midi_data_len(b);
            me->have = 0;
            if (me->need == 0) me->cur = 0;
        } else {
            me->running = b;
            me->cur = b;
            me->need = midi_data_len(b);
            me->have = 0;
        }
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (o->latched) {
                fwd(me, p, b);
                o->last_status = (b < 0xF0) ? b : 0;
            }
        }
        return;
    }

    /* ---- data byte ---- */
    if (me->in_sysex) {
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (o->latched && !o->skip) fwd(me, p, b);
        }
        return;
    }

    if (me->cur != 0 && me->have < me->need) {
        /* Continuation of the message in progress. */
        me->have++;
        if (me->have >= me->need) { me->cur = 0; me->forced = false; }
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (o->latched && !o->skip) fwd(me, p, b);
        }
        return;
    }

    if (me->running != 0) {
        /* First data byte of a new running-status message. */
        start_msg(me);
        me->cur = me->running;
        me->need = midi_data_len(me->running);
        me->have = 1;
        if (me->have >= me->need) me->cur = 0;
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (!o->latched) continue;
            if (o->last_status != me->running) {
                /* A merged message changed this output's running status. */
                fwd(me, p, me->running);
                o->last_status = me->running;
            }
            fwd(me, p, b);
        }
        return;
    }

    /* Data byte with no valid status (e.g. after system common): the MIDI
     * spec says ignore it. Forwarding it could attach it to a merged
     * message's running status downstream, so drop and count. */
    me->stats.orphan_data++;
}

void ME_RAMFUNC(me_service)(me_t *me, uint64_t now_us) {
    /* 1. Generated realtime bytes: immediate, anywhere in the stream. */
    while (spsc_count(&me->rtq_idx) > 0) {
        uint8_t b = me->rtq[spsc_tail_slot(&me->rtq_idx, ME_RTQ_SIZE)];
        spsc_pop_commit(&me->rtq_idx);
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            if (!me->set->gen[p]) continue;
            /* A saturated line cannot carry extra bytes: shed generated
             * realtime rather than let thru traffic back up without limit. */
            if (outq_fill(&me->out[p]) > ME_GEN_BACKLOG_MAX) me->out[p].gen_shed++;
            else outrt_push(&me->out[p], b);
        }
        me->stats.gen_rt++;
    }

    /* 2. Stall detection: input stopped part-way through a message. */
    if (!me_at_boundary(me)) {
        uint64_t limit = me->in_sysex ? ME_STALL_SYSEX_US : ME_STALL_MSG_US;
        if (now_us - me->last_in_us > limit) {
            me->forced = true;
            me->stats.stalls++;
        } else {
            return;
        }
    }

    /* 3. Generated messages at the input message boundary. */
    while (spsc_count(&me->genq_idx) > 0) {
        const me_gen_msg_t *m = &me->genq[spsc_tail_slot(&me->genq_idx, ME_GENQ_SIZE)];
        uint8_t len = m->len;
        if (len == 0 || len > ME_GEN_MAX_LEN || !midi_is_status(m->data[0])) {
            spsc_pop_commit(&me->genq_idx); /* malformed: discard */
            continue;
        }
        /* Bound the backlog the generator may add in front of thru traffic. */
        bool room = true;
        for (int p = 0; p < ME_NUM_OUTS; p++)
            if (me->set->gen[p] && outq_fill(&me->out[p]) + len > ME_GEN_BACKLOG_MAX)
                room = false;
        if (!room) break;

        uint8_t st = m->data[0];
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (!me->set->gen[p]) continue;
            if (me->forced && o->latched) o->skip = true;
            for (uint8_t i = 0; i < len; i++) outq_push(o, m->data[i]);
            o->last_status = (st < 0xF0) ? st : 0;
        }
        spsc_pop_commit(&me->genq_idx);
        me->stats.gen_msgs++;
    }
}

bool ME_RAMFUNC(me_out_pop)(me_t *me, int port, uint8_t *b) {
    me_out_t *o = &me->out[port];
    if (o->rh != o->rt_t) {
        *b = o->rt[o->rt_t & (ME_OUT_RTQ_SIZE - 1u)];
        o->rt_t++;
        return true;
    }
    if (o->qh != o->qt) {
        *b = o->q[o->qt & (ME_OUTQ_SIZE - 1u)];
        o->qt++;
        return true;
    }
    return false;
}

uint32_t me_out_pending(const me_t *me, int port) {
    const me_out_t *o = &me->out[port];
    return (o->qh - o->qt) + (o->rh - o->rt_t);
}

void ME_RAMFUNC(me_mon_push)(me_t *me, uint8_t b, uint8_t flags, uint64_t t_us) {
    if (spsc_free(&me->mon_idx, ME_MONQ_SIZE) == 0) {
        me->stats.mon_overflow++;
        me->mon_gap = true;
        return;
    }
    me_mon_entry_t *e = &me->mon[spsc_head_slot(&me->mon_idx, ME_MONQ_SIZE)];
    e->t_us = t_us;
    e->byte = b;
    e->flags = (uint8_t)(flags | (me->mon_gap ? ME_MON_F_GAP : 0u));
    me->mon_gap = false;
    spsc_push_commit(&me->mon_idx);
}

/* ------------------------------------------------------------------------ */
/* core0 side                                                                */
/* ------------------------------------------------------------------------ */

bool me_gen_push(me_t *me, const uint8_t *msg, uint8_t len) {
    if (len == 0 || len > ME_GEN_MAX_LEN) return false;
    if (spsc_free(&me->genq_idx, ME_GENQ_SIZE) == 0) return false;
    me_gen_msg_t *m = &me->genq[spsc_head_slot(&me->genq_idx, ME_GENQ_SIZE)];
    memcpy(m->data, msg, len);
    m->len = len;
    spsc_push_commit(&me->genq_idx);
    return true;
}

bool me_gen_rt_push(me_t *me, uint8_t rt) {
    if (!midi_is_realtime(rt)) return false;
    if (spsc_free(&me->rtq_idx, ME_RTQ_SIZE) == 0) return false;
    me->rtq[spsc_head_slot(&me->rtq_idx, ME_RTQ_SIZE)] = rt;
    spsc_push_commit(&me->rtq_idx);
    return true;
}

bool me_mon_pop(me_t *me, me_mon_entry_t *e) {
    if (spsc_count(&me->mon_idx) == 0) return false;
    *e = me->mon[spsc_tail_slot(&me->mon_idx, ME_MONQ_SIZE)];
    spsc_pop_commit(&me->mon_idx);
    return true;
}
