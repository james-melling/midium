/*
 * midi_parser.c - MIDI 1.0 stream parser. See midi_parser.h.
 */
#include <string.h>
#include "midi_parser.h"
#include "midi_defs.h"

void midi_parser_init(midi_parser_t *p, midi_msg_cb cb, void *ctx) {
    memset(p, 0, sizeof(*p));
    p->cb = cb;
    p->ctx = ctx;
}

void midi_parser_reset(midi_parser_t *p) {
    p->running = 0;
    p->cur = 0;
    p->need = p->have = 0;
    p->in_sysex = false;
}

static void emit_simple(midi_parser_t *p, uint8_t type, uint8_t status,
                        uint8_t d1, uint8_t d2, uint64_t t) {
    midi_msg_t m;
    m.t_us = t;
    m.type = type;
    m.status = status;
    m.d1 = d1;
    m.d2 = d2;
    m.flags = 0;
    m.src = p->src;
    m.sx_len = 0;
    p->cb(&m, p->ctx);
}

uint8_t midi_type_of_status(uint8_t s) {
    switch (s & 0xF0u) {
    case 0x80: return MT_NOTE_OFF;
    case 0x90: return MT_NOTE_ON;
    case 0xA0: return MT_POLY_AT;
    case 0xB0: return MT_CC;
    case 0xC0: return MT_PROGRAM;
    case 0xD0: return MT_CHAN_AT;
    case 0xE0: return MT_PITCH_BEND;
    default: break;
    }
    switch (s) {
    case MIDI_SYSEX:        return MT_SYSEX;
    case MIDI_MTC_QF:       return MT_MTC_QF;
    case MIDI_SONG_POS:     return MT_SONG_POS;
    case MIDI_SONG_SEL:     return MT_SONG_SEL;
    case MIDI_TUNE_REQ:     return MT_TUNE_REQ;
    case MIDI_CLOCK:        return MT_CLOCK;
    case MIDI_START:        return MT_START;
    case MIDI_CONTINUE:     return MT_CONTINUE;
    case MIDI_STOP:         return MT_STOP;
    case MIDI_ACTIVE_SENSE: return MT_ACTIVE_SENSE;
    case MIDI_RESET:        return MT_RESET;
    default:                return MT_UNDEFINED;
    }
}

static void sysex_add(midi_parser_t *p, uint8_t b) {
    if (p->sx.sx_len < MIDI_SX_STORE) p->sx.sx[p->sx.sx_len] = b;
    else p->sx.flags |= MSG_F_SX_TRUNC;
    if (p->sx.sx_len < 0xFFFFu) p->sx.sx_len++;
}

static void sysex_end(midi_parser_t *p, bool terminated) {
    if (!terminated) p->sx.flags |= MSG_F_SX_UNTERM;
    p->in_sysex = false;
    p->cb(&p->sx, p->ctx);
}

static void complete(midi_parser_t *p) {
    uint8_t s = p->cur;
    emit_simple(p, midi_type_of_status(s), s, p->d[0], p->need > 1 ? p->d[1] : 0, p->t_start);
    p->cur = 0;
    p->have = 0;
}

void midi_parser_feed(midi_parser_t *p, uint8_t b, uint64_t t) {
    if (midi_is_realtime(b)) {
        /* Interleaved anywhere; does not disturb any other state. */
        emit_simple(p, midi_type_of_status(b), b, 0, 0, t);
        return;
    }

    if (midi_is_status(b)) {
        if (p->in_sysex) {
            if (b == MIDI_EOX) {
                sysex_add(p, b);
                sysex_end(p, true);
                return;
            }
            sysex_end(p, false);
        }
        /* An incomplete message in progress is abandoned (as a receiver would). */
        p->cur = 0;
        p->have = 0;

        if (b == MIDI_SYSEX) {
            p->running = 0;
            p->in_sysex = true;
            memset(&p->sx, 0, sizeof(p->sx));
            p->sx.t_us = t;
            p->sx.type = MT_SYSEX;
            p->sx.status = b;
            p->sx.src = p->src;
            sysex_add(p, b);
            return;
        }
        if (b >= 0xF0) {
            p->running = 0;           /* system common clears running status */
            uint8_t n = midi_data_len(b);
            if (n == 0) {
                emit_simple(p, midi_type_of_status(b), b, 0, 0, t);
                return;
            }
            p->cur = b;
            p->need = n;
            p->t_start = t;
            return;
        }
        p->running = b;
        p->cur = b;
        p->need = midi_data_len(b);
        p->t_start = t;
        return;
    }

    /* data byte */
    if (p->in_sysex) { sysex_add(p, b); return; }

    if (p->cur == 0) {
        if (p->running == 0) { p->stray_data++; return; }
        p->cur = p->running;       /* running status */
        p->need = midi_data_len(p->running);
        p->have = 0;
        p->t_start = t;
    }
    p->d[p->have++] = b;
    if (p->have >= p->need) complete(p);
}
