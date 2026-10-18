/*
 * midi_decode.c - MIDI message -> short text. See midi_decode.h.
 */
#include <stdio.h>
#include <string.h>
#include "midi_decode.h"
#include "midi_defs.h"
#include "msc_decode.h"

static const char *const NOTE_NAMES[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void midi_note_name(uint8_t note, bool c3, char *buf, size_t n) {
    int octave = (int)(note / 12u) - (c3 ? 2 : 1);
    snprintf(buf, n, "%s%d", NOTE_NAMES[note % 12u], octave);
}

void midi_format_time(uint64_t t_us, char *buf, size_t n) {
    uint64_t ms = t_us / 1000u;
    unsigned msec = (unsigned)(ms % 1000u);
    unsigned sec  = (unsigned)((ms / 1000u) % 60u);
    unsigned min  = (unsigned)(ms / 60000u);
    snprintf(buf, n, "%02u:%02u.%03u", min % 100u, sec, msec);
}

static const char *mmc_cmd_name(uint8_t c) {
    switch (c) {
    case 0x01: return "STOP";
    case 0x02: return "PLAY";
    case 0x03: return "DEF PLAY";
    case 0x04: return "FFWD";
    case 0x05: return "REWIND";
    case 0x06: return "REC STROBE";
    case 0x07: return "REC EXIT";
    case 0x08: return "REC PAUSE";
    case 0x09: return "PAUSE";
    case 0x0A: return "EJECT";
    case 0x0B: return "CHASE";
    case 0x0D: return "RESET";
    case 0x40: return "WRITE";
    case 0x44: return "LOCATE";
    case 0x47: return "SHUTTLE";
    default:   return NULL;
    }
}

/* Stored sysex bytes available (may be fewer than sx_len if truncated). */
static unsigned sx_stored(const midi_msg_t *m) {
    return m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
}

static msg_class_t decode_sysex(const midi_msg_t *m, unsigned width, char *buf, size_t n) {
    const uint8_t *s = m->sx;
    unsigned have = sx_stored(m);
    const char *bang = (m->flags & MSG_F_SX_UNTERM) ? "!" : "";

    /* Universal realtime: F0 7F <dev> <sub-id1> ... */
    msc_msg_t d;
    if (msc_decode(s, have, m->sx_len, &d)) {   /* MIDI Show Control: msc_decode.c */
        msc_summary(&d, width, buf, n);
        return CLS_SYSEX;
    }
    if (have >= 6 && s[1] == 0x7F) {
        uint8_t sub1 = s[3];
        if (sub1 == 0x06) {              /* MIDI Machine Control command */
            const char *cn = mmc_cmd_name(s[4]);
            if (cn) snprintf(buf, n, "MMC %s%s", cn, bang);
            else snprintf(buf, n, "MMC CMD%02X%s", s[4], bang);
            return CLS_SYSEX;
        }
        if (sub1 == 0x01 && s[4] == 0x01 && have >= 9) {   /* MTC full frame */
            snprintf(buf, n, "MTC %02u:%02u:%02u:%02u%s",
                     (unsigned)(s[5] & 0x1Fu), (unsigned)s[6], (unsigned)s[7],
                     (unsigned)s[8], bang);
            return CLS_SYSEX;
        }
    }

    char id[10] = "";
    if (have >= 2) {
        if (s[1] == 0x00 && have >= 4) snprintf(id, sizeof id, " id%02X%02X%02X", s[1], s[2], s[3]);
        else if (s[1] != MIDI_EOX) snprintf(id, sizeof id, " id%02X", s[1]);
    }
    snprintf(buf, n, "SYSEX %ub%s%s", (unsigned)m->sx_len, id, bang);
    return CLS_SYSEX;
}

msg_class_t midi_decode(const midi_msg_t *m, const decode_opts_t *o, char *buf, size_t n) {
    char note[6];
    unsigned ch = (unsigned)(m->status & 0x0Fu) + 1u;
    bool c3 = o && o->note_c3;

    switch ((midi_type_t)m->type) {
    case MT_NOTE_ON:
        midi_note_name(m->d1, c3, note, sizeof note);
        snprintf(buf, n, "NOTE ON %2u %-4s %3u", ch, note, (unsigned)m->d2);
        return CLS_NOTE;
    case MT_NOTE_OFF:
        midi_note_name(m->d1, c3, note, sizeof note);
        snprintf(buf, n, "NOTE OFF%2u %-4s %3u", ch, note, (unsigned)m->d2);
        return CLS_NOTE;
    case MT_POLY_AT:
        midi_note_name(m->d1, c3, note, sizeof note);
        snprintf(buf, n, "PAT %2u %-4s %3u", ch, note, (unsigned)m->d2);
        return CLS_NOTE;
    case MT_CC:
        snprintf(buf, n, "CC %2u #%u=%u", ch, (unsigned)m->d1, (unsigned)m->d2);
        return CLS_CC;
    case MT_PROGRAM:
        snprintf(buf, n, "PC %2u %u", ch, (unsigned)m->d1);
        return CLS_PC;
    case MT_CHAN_AT:
        snprintf(buf, n, "AT %2u %u", ch, (unsigned)m->d1);
        return CLS_CC;
    case MT_PITCH_BEND: {
        int v = (int)((unsigned)m->d1 | ((unsigned)m->d2 << 7)) - 8192;
        snprintf(buf, n, "PB %2u %+d", ch, v);
        return CLS_CC;
    }
    case MT_SYSEX:
        return decode_sysex(m, (o && o->width) ? o->width : MIDI_TEXT_COLS, buf, n);
    case MT_MTC_QF:
        snprintf(buf, n, "MTC QF %u:%X", (unsigned)(m->d1 >> 4) & 7u, (unsigned)m->d1 & 0x0Fu);
        return CLS_REALTIME;
    case MT_SONG_POS:
        snprintf(buf, n, "SONG POS %u", (unsigned)m->d1 | ((unsigned)m->d2 << 7));
        return CLS_PC;
    case MT_SONG_SEL:
        snprintf(buf, n, "SONG SEL %u", (unsigned)m->d1);
        return CLS_PC;
    case MT_TUNE_REQ:     snprintf(buf, n, "TUNE REQUEST");  return CLS_PC;
    case MT_CLOCK:        snprintf(buf, n, "CLOCK");         return CLS_REALTIME;
    case MT_START:        snprintf(buf, n, "START");         return CLS_REALTIME;
    case MT_CONTINUE:     snprintf(buf, n, "CONTINUE");      return CLS_REALTIME;
    case MT_STOP:         snprintf(buf, n, "STOP");          return CLS_REALTIME;
    case MT_ACTIVE_SENSE: snprintf(buf, n, "ACTIVE SENSE");  return CLS_REALTIME;
    case MT_RESET:        snprintf(buf, n, "SYSTEM RESET");  return CLS_REALTIME;
    case MT_UNDEFINED:
        snprintf(buf, n, "UNDEF %02X", (unsigned)m->status);
        return CLS_DIM;
    case MT_ERROR:
        switch (m->d1) {
        case MSG_ERR_FRAMING: snprintf(buf, n, "ERR FRAMING");     break;
        case MSG_ERR_BREAK:   snprintf(buf, n, "ERR BREAK");       break;
        case MSG_ERR_OVERRUN: snprintf(buf, n, "ERR RX OVERRUN");  break;
        case MSG_ERR_GAP:     snprintf(buf, n, "ERR MONITOR GAP"); break;
        default:              snprintf(buf, n, "ERR %u", (unsigned)m->d1); break;
        }
        return CLS_ERROR;
    default:
        snprintf(buf, n, "?");
        return CLS_ERROR;
    }
}

bool midi_is_noisy_realtime(const midi_msg_t *m) {
    return m->type == MT_CLOCK || m->type == MT_ACTIVE_SENSE;
}

const char *midi_type_label(midi_type_t t) {
    static const char *const L[MT_COUNT] = {
        "Note Off", "Note On", "Poly AT", "CC", "Program", "Chan AT",
        "Pitch Bend", "SysEx", "MTC QF", "Song Pos", "Song Sel", "Tune Req",
        "Clock", "Start", "Continue", "Stop", "Active Sns", "Reset",
        "Undefined", "Errors"
    };
    return (t < MT_COUNT) ? L[t] : "?";
}
