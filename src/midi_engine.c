/*
 * midi_engine.c - byte-level MIDI thru + merge engine. See midi_engine.h.
 *
 * Portable C: no Pico SDK headers. On target, ME_RAMFUNC places the hot
 * functions in SRAM so flash (XIP) activity cannot stall the thru path.
 * Every function on the core1 path is either ME_RAMFUNC or static inline,
 * and nothing here calls into the C library (no memcpy / memset), so the
 * whole of core1 runs from SRAM. Check with the objdump recipe in README.
 */
#include <string.h>
#include "midi_engine.h"
#include "midi_defs.h"

/* ------------------------------------------------------------------------ */
/* Output queues and lanes (core1-private, so plain indices)                 */
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

static inline bool lane_empty(const me_lane_t *l) { return l->h == l->t; }
static inline uint16_t lane_head(const me_lane_t *l) { return l->e[l->t & (ME_LANE_SIZE - 1u)]; }

static inline uint32_t gl_fill(const me_out_t *o) { return o->gh - o->gt; }

/*
 * Routing stage hook.
 *
 * TODO(M4): filter / transform rules (channel remap, per-type filters). The
 * merger already hands over complete messages at known boundaries, so M4
 * rules can act on whole messages per output here. In 0.3 this is a pure
 * pass-through.
 */
static inline bool me_route_byte(me_t *me, int port, uint8_t b) {
    (void)me; (void)port; (void)b;
    return true;
}

static inline void emit(me_t *me, int port, uint8_t b) {
    if (me_route_byte(me, port, b)) outq_push(&me->out[port], b);
}

static inline void note_status(me_out_t *o, uint8_t st) {
    o->last_status = (st < 0xF0u) ? st : 0;
}

/* ------------------------------------------------------------------------ */

void me_init(me_t *me, me_settings_t *set) {
    memset(me, 0, sizeof(*me));
    me->set = set;
    for (int p = 0; p < ME_NUM_OUTS; p++) me->out[p].owner = ME_SRC_NONE;
    spsc_init(&me->genq_idx);
    spsc_init(&me->rtq_idx);
    spsc_init(&me->mon_idx);
}

bool ME_RAMFUNC(me_out_at_boundary)(const me_t *me, int port) {
    return me->out[port].owner == ME_SRC_NONE;
}

/* Does any source other than the owner have something for this output? */
static inline bool others_waiting(const me_out_t *o) {
    if (gl_fill(o)) return true;
    for (int i = 0; i < ME_NUM_INS; i++)
        if (i != o->owner && !lane_empty(&o->lane[i])) return true;
    return false;
}

/* May the message at the head of input `in`'s lane for `port` start now
 * without risking a long hold of the output? Yes if it has fully arrived,
 * or if its input is still sending at line rate. A message that is only
 * trickling in (no byte for ME_TRICKLE_US) waits while another input has a
 * message ready. The head is the input's current message only if it starts
 * at msg_start and the input is still inside a message (an unterminated
 * sysex counts as still arriving). */
static inline bool head_ready(const me_t *me, int in, int port, uint64_t now_us) {
    const me_in_t *s = &me->in[in];
    bool open = s->cur != 0 || s->in_sysex;
    if (!open || me->out[port].lane[in].t != s->msg_start[port]) return true;
    return now_us - s->last_in_us <= ME_TRICKLE_US;
}

/*
 * The merger for one output: move bytes from the lanes and the gen lane to
 * the TX queue, one complete message at a time. Called after every input
 * byte routed to this output and from me_service().
 */
static void ME_RAMFUNC(pump)(me_t *me, int port, uint64_t now_us) {
    me_out_t *o = &me->out[port];
    for (;;) {
        if (o->owner != ME_SRC_NONE) {
            /* Continue the owner's message as far as it has arrived. */
            me_lane_t *l = &o->lane[o->owner];
            while (!lane_empty(l)) {
                uint16_t e = lane_head(l);
                if (e & ME_L_START) { o->owner = ME_SRC_NONE; break; } /* ended without F7 */
                l->t++;
                emit(me, port, (uint8_t)e);
                if (e & ME_L_END) {
                    if ((uint8_t)e == MIDI_EOX) o->last_status = 0;
                    o->owner = ME_SRC_NONE;
                    break;
                }
            }
            if (o->owner != ME_SRC_NONE) {
                /* Mid-message and waiting for the owner's next byte. */
                me_in_t *s = &me->in[o->owner];
                uint64_t limit = s->in_sysex ? ME_STALL_SYSEX_US : ME_STALL_MSG_US;
                if (now_us - s->last_in_us <= limit || !others_waiting(o)) return;
                /* Stalled and someone else is waiting: force the boundary. */
                s->skip[port] = true;
                if (!s->stall_counted) {
                    s->stall_counted = true;
                    s->st.stalls++;
                    me->stats.stalls++;
                }
                o->owner = ME_SRC_NONE;
            }
        }

        /* ---- at a message boundary on this output ---- */

        /* 1. Generator, bounded so it can only add a little thru latency. */
        if (gl_fill(o)) {
            uint8_t len = o->gl[o->gt & (ME_GENLANE_SIZE - 1u)];
            if (outq_fill(o) + len <= ME_GEN_BACKLOG_MAX) {
                o->gt++;
                note_status(o, o->gl[o->gt & (ME_GENLANE_SIZE - 1u)]);
                for (uint8_t k = 0; k < len; k++) {
                    emit(me, port, o->gl[o->gt & (ME_GENLANE_SIZE - 1u)]);
                    o->gt++;
                }
                continue;
            }
        }

        /* 2. Inputs, round-robin, one message each. A message that is
         * trickling in goes after one that is ready (see head_ready), so a
         * slow sender does not hold the output while the other input has a
         * message waiting. With one candidate it goes at once. */
        int pick = -1, partial = -1;
        for (int k = 0; k < ME_NUM_INS; k++) {
            int i = (o->rr + k) % ME_NUM_INS;
            if (lane_empty(&o->lane[i])) continue;
            if (head_ready(me, i, port, now_us)) { pick = i; break; }
            if (partial < 0) partial = i;
        }
        if (pick < 0) pick = partial;
        if (pick < 0) return;
        if (outq_fill(o) > ME_MERGE_BACKLOG_MAX) return;   /* line overloaded: wait in the lane */
        o->rr = (uint8_t)((pick + 1) % ME_NUM_INS);

        me_lane_t *l = &o->lane[pick];
        uint16_t e = lane_head(l);
        l->t++;
        uint8_t st = (uint8_t)e;
        /* (e always has ME_L_START here: lanes only ever hold whole messages
         * from their start, see lane_push.) */
        if (!(e & ME_L_IMPL) || o->last_status != st) emit(me, port, st);
        note_status(o, st);
        if (!(e & ME_L_END)) o->owner = (uint8_t)pick;
    }
}

/* Append one entry of input `in`'s current message to its lane for `port`. */
static inline void lane_push(me_t *me, int in, int port, uint16_t e) {
    me_in_t *s = &me->in[in];
    me_out_t *o = &me->out[port];
    me_lane_t *l = &o->lane[in];
    if (e & ME_L_START) s->msg_start[port] = l->h;
    if (l->h - l->t >= ME_LANE_SIZE) {
        if (o->owner == in) {
            /* Cannot happen (the owner's lane is flushed on every byte), but
             * never cut a message that has started: queue it directly. */
            emit(me, port, (uint8_t)e);
            return;
        }
        /* Drop the whole message: take back what is buffered, skip the rest. */
        l->h = s->msg_start[port];
        s->skip[port] = true;
        o->merge_drops[in]++;
        return;
    }
    l->e[l->h & (ME_LANE_SIZE - 1u)] = e;
    l->h++;
}

/* Deliver one non-realtime entry to every output the current message goes to. */
static inline void deliver(me_t *me, int in, uint16_t e, uint64_t now_us) {
    me_in_t *s = &me->in[in];
    for (int p = 0; p < ME_NUM_OUTS; p++) {
        if (!s->latched[p] || s->skip[p]) continue;
        lane_push(me, in, p, e);
        pump(me, p, now_us);
    }
}

/* Start of a new input message: latch routing per output. */
static inline void start_msg(me_t *me, int in) {
    me_in_t *s = &me->in[in];
    s->stall_counted = false;
    for (int p = 0; p < ME_NUM_OUTS; p++) {
        s->latched[p] = me->set->src[p][in];
        s->skip[p] = false;
    }
}

void ME_RAMFUNC(me_input_byte)(me_t *me, int in, uint8_t b, uint64_t now_us) {
    me_in_t *s = &me->in[in];
    s->st.bytes_in++;
    s->last_in_us = now_us;

    /* Realtime: forward immediately, ahead of anything queued. Does not touch
     * parser state, latches or running status. */
    if (midi_is_realtime(b)) {
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            if (!me->set->src[p][in] || !me_route_byte(me, p, b)) continue;
            me_out_t *o = &me->out[p];
            if (outq_fill(o) > ME_RT_SHED_FILL) o->rt_shed++;
            else outrt_push(o, b);
        }
        return;
    }

    if (midi_is_status(b)) {
        if (b == MIDI_EOX && s->in_sysex) {
            /* End of the sysex in progress: belongs to the message latched at F0. */
            s->in_sysex = false;
            deliver(me, in, (uint16_t)(b | ME_L_END), now_us);
            return;
        }

        /* Any other status byte starts a new message (and implicitly ends an
         * unterminated sysex). */
        s->in_sysex = false;
        start_msg(me, in);
        uint16_t e = (uint16_t)(b | ME_L_START);
        if (b == MIDI_SYSEX) {
            s->in_sysex = true;
            s->running = 0;
            s->cur = 0;
        } else if (b >= 0xF0) {
            /* System common (incl. stray F7): clears running status. */
            s->running = 0;
            s->cur = b;
            s->need = midi_data_len(b);
            s->have = 0;
            if (s->need == 0) { s->cur = 0; e |= ME_L_END; }
        } else {
            s->running = b;
            s->cur = b;
            s->need = midi_data_len(b);
            s->have = 0;
        }
        deliver(me, in, e, now_us);
        return;
    }

    /* ---- data byte ---- */
    if (s->in_sysex) {
        deliver(me, in, b, now_us);
        return;
    }

    if (s->cur != 0 && s->have < s->need) {
        /* Continuation of the message in progress. */
        s->have++;
        uint16_t e = b;
        if (s->have >= s->need) { s->cur = 0; e |= ME_L_END; }
        deliver(me, in, e, now_us);
        return;
    }

    if (s->running != 0) {
        /* First data byte of a new running-status message. The status byte
         * goes in the lane marked implicit: the merger sends it only if this
         * output's last status differs. */
        start_msg(me, in);
        s->cur = s->running;
        s->need = midi_data_len(s->running);
        s->have = 1;
        uint16_t e = b;
        if (s->have >= s->need) { s->cur = 0; e |= ME_L_END; }
        deliver(me, in, (uint16_t)(s->running | ME_L_START | ME_L_IMPL), now_us);
        deliver(me, in, e, now_us);
        return;
    }

    /* Data byte with no valid status (e.g. after system common): the MIDI
     * spec says ignore it. Forwarding it could attach it to a merged
     * message's running status downstream, so drop and count. */
    s->st.orphan_data++;
}

void ME_RAMFUNC(me_service)(me_t *me, uint64_t now_us) {
    /* 1. Generated realtime bytes: immediate, anywhere in the stream. */
    while (spsc_count(&me->rtq_idx) > 0) {
        uint8_t b = me->rtq[spsc_tail_slot(&me->rtq_idx, ME_RTQ_SIZE)];
        spsc_pop_commit(&me->rtq_idx);
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            if (!me->set->src[p][ME_SRC_GEN]) continue;
            /* A saturated line cannot carry extra bytes: shed generated
             * realtime rather than let thru traffic back up without limit. */
            if (outq_fill(&me->out[p]) > ME_GEN_BACKLOG_MAX) me->out[p].gen_shed++;
            else outrt_push(&me->out[p], b);
        }
        me->stats.gen_rt++;
    }

    /* 2. Generated messages into each routed output's gen lane. */
    while (spsc_count(&me->genq_idx) > 0) {
        const me_gen_msg_t *m = &me->genq[spsc_tail_slot(&me->genq_idx, ME_GENQ_SIZE)];
        uint8_t len = m->len;
        if (len == 0 || len > ME_GEN_MAX_LEN || !midi_is_status(m->data[0])) {
            spsc_pop_commit(&me->genq_idx); /* malformed: discard */
            continue;
        }
        bool room = true;
        for (int p = 0; p < ME_NUM_OUTS; p++)
            if (me->set->src[p][ME_SRC_GEN] && gl_fill(&me->out[p]) + len + 1u > ME_GENLANE_SIZE)
                room = false;
        if (!room) break;
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            me_out_t *o = &me->out[p];
            if (!me->set->src[p][ME_SRC_GEN]) continue;
            o->gl[o->gh & (ME_GENLANE_SIZE - 1u)] = len;
            o->gh++;
            for (uint8_t i = 0; i < len; i++) {
                o->gl[o->gh & (ME_GENLANE_SIZE - 1u)] = m->data[i];
                o->gh++;
            }
        }
        spsc_pop_commit(&me->genq_idx);
        me->stats.gen_msgs++;
    }

    /* 3. Mergers: generator at boundaries, waiting input messages, stalls. */
    for (int p = 0; p < ME_NUM_OUTS; p++) pump(me, p, now_us);
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

bool me_idle(const me_t *me) {
    if (spsc_count(&me->genq_idx) || spsc_count(&me->rtq_idx)) return false;
    for (int p = 0; p < ME_NUM_OUTS; p++) {
        const me_out_t *o = &me->out[p];
        if (me_out_pending(me, p) || gl_fill(o)) return false;
        for (int i = 0; i < ME_NUM_INS; i++) if (!lane_empty(&o->lane[i])) return false;
    }
    return true;
}

void ME_RAMFUNC(me_mon_push)(me_t *me, int in, uint8_t b, uint8_t flags, uint64_t t_us) {
    uint8_t bit = (uint8_t)(1u << in);
    if (spsc_free(&me->mon_idx, ME_MONQ_SIZE) == 0) {
        me->in[in].st.mon_overflow++;
        me->mon_gap |= bit;
        return;
    }
    me_mon_entry_t *e = &me->mon[spsc_head_slot(&me->mon_idx, ME_MONQ_SIZE)];
    e->t_us = t_us;
    e->byte = b;
    e->src = (uint8_t)in;
    e->flags = (uint8_t)(flags | ((me->mon_gap & bit) ? ME_MON_F_GAP : 0u));
    me->mon_gap &= (uint8_t)~bit;
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
