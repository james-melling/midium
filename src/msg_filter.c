/*
 * msg_filter.c - monitor / log browser filters. See msg_filter.h.
 */
#include "msg_filter.h"
#include "msc_decode.h"

filter_cat_t msg_filter_cat(const midi_msg_t *m) {
    switch ((midi_type_t)m->type) {
    case MT_NOTE_OFF:
    case MT_NOTE_ON:      return FC_NOTE;
    case MT_POLY_AT:      return FC_POLY_AT;
    case MT_CC:           return FC_CC;
    case MT_PROGRAM:      return FC_PROGRAM;
    case MT_CHAN_AT:      return FC_CHAN_AT;
    case MT_PITCH_BEND:   return FC_PITCH_BEND;
    case MT_SYSEX: {
        unsigned have = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
        if (msc_is_msc(m->sx, have)) return FC_MSC;
        if (have >= 5 && m->sx[1] == 0x7F && (m->sx[3] == 0x06 || m->sx[3] == 0x07)) return FC_MMC;
        if (have >= 10 && m->sx[1] == 0x7F && m->sx[3] == 0x01 && m->sx[4] == 0x01) return FC_MTC_FULL;
        return FC_SYSEX;
    }
    case MT_MTC_QF:       return FC_MTC_QF;
    case MT_SONG_POS:
    case MT_SONG_SEL:     return FC_SONG;
    case MT_TUNE_REQ:     return FC_TUNE;
    case MT_CLOCK:        return FC_CLOCK;
    case MT_START:
    case MT_CONTINUE:
    case MT_STOP:         return FC_TRANSPORT;
    case MT_ACTIVE_SENSE: return FC_ACTIVE_SENSE;
    case MT_RESET:        return FC_RESET;
    case MT_ERROR:        return FC_ERROR;
    case MT_UNDEFINED:
    default:              return FC_UNDEFINED;
    }
}

const char *msg_filter_cat_label(filter_cat_t c) {
    static const char *const L[FC_COUNT] = {
        "Note on/off", "Poly AT", "CC", "Program change", "Channel AT", "Pitch bend",
        "SysEx (other)", "MSC", "MMC", "MTC quarter fr", "MTC full frame", "Song pos/sel",
        "Tune request", "Clock", "Start/Stop/Cont", "Active sensing", "System reset",
        "Undefined", "Errors",
    };
    return (unsigned)c < FC_COUNT ? L[c] : "?";
}

const char *msg_filter_preset_name(filter_preset_t p) {
    static const char *const N[FP_COUNT] = { "All", "Show control only", "No realtime/TC", "None" };
    return (unsigned)p < FP_COUNT ? N[p] : "?";
}

static uint32_t preset_mask(filter_preset_t p) {
    const uint32_t types = FC_ALL_MASK & ~FC_BIT(FC_ERROR);
    switch (p) {
    case FP_ALL:          return types;
    case FP_SHOW_CONTROL: return FC_BIT(FC_MSC) | FC_BIT(FC_MMC) | FC_BIT(FC_MTC_FULL) |
                                 FC_BIT(FC_TRANSPORT) | FC_BIT(FC_PROGRAM);
    case FP_NO_REALTIME:  return types & ~(FC_BIT(FC_CLOCK) | FC_BIT(FC_ACTIVE_SENSE) | FC_BIT(FC_MTC_QF));
    case FP_NONE:
    default:              return 0;
    }
}

void msg_filter_apply_preset(msg_filter_t *f, filter_preset_t p) {
    f->types = (f->types & FC_BIT(FC_ERROR)) | preset_mask(p);
}

bool msg_filter_preset_active(const msg_filter_t *f, filter_preset_t p) {
    return (f->types & ~FC_BIT(FC_ERROR)) == preset_mask(p);
}

void msg_filter_defaults(msg_filter_t *f) {
    f->types = FC_BIT(FC_ERROR);
    msg_filter_apply_preset(f, FP_NO_REALTIME);
    f->channel = -1;
    f->inputs = SHOW_IN_BOTH;
    f->msc_dev = -1;
}

bool msg_filter_type_pass(const msg_filter_t *f, const midi_msg_t *m) {
    return (f->types & FC_BIT(msg_filter_cat(m))) != 0;
}

bool msg_filter_pass(const msg_filter_t *f, const midi_msg_t *m) {
    /* Input filter, errors included: they belong to one input too. */
    if (f->inputs == SHOW_IN_A && m->src != 0) return false;
    if (f->inputs == SHOW_IN_B && m->src != 1) return false;
    filter_cat_t c = msg_filter_cat(m);
    if (!(f->types & FC_BIT(c))) return false;
    /* Channel filter: channel voice messages only. */
    if (f->channel >= 0 && m->type <= MT_PITCH_BEND && (m->status & 0x0F) != (uint8_t)f->channel)
        return false;
    /* MSC device filter: that device_ID and the all-call ID (7F), which
     * every device must obey; group IDs are not matched (membership unknown). */
    if (c == FC_MSC && f->msc_dev >= 0 && m->sx[2] != (uint8_t)f->msc_dev && m->sx[2] != MSC_DEV_ALL)
        return false;
    return true;
}
