/*
 * msc_decode.c - MIDI Show Control 1.1.1 decoder / formatter / builder.
 * See msc_decode.h. Portable C (unit tested on the host).
 *
 * Data layouts (MSC 1.1.1 section 5), after F0 7F <dev> 02 <fmt> <cmd>:
 *   GO STOP RESUME GO_OFF GO/JAM_CLOCK   [Q_number [00 Q_list [00 Q_path]]]
 *   LOAD                                  Q_number (required) [00 Q_list [00 Q_path]]
 *   TIMED_GO                              hr mn sc fr ff  [cue fields]
 *   SET                                   cc cc vv vv [hr mn sc fr ff]  (LSB first)
 *   FIRE                                  mm
 *   ALL_OFF RESTORE RESET                 (none)
 *   STANDBY_+/- SEQUENCE_+/- START/STOP/ZERO_CLOCK MTC_CHASE_ON/OFF   [Q_list]
 *   OPEN/CLOSE_CUE_LIST                   Q_list (required)
 *   OPEN/CLOSE_CUE_PATH                   Q_path (required)
 *   SET_CLOCK                             hr mn sc fr (ff|st) [Q_list]
 *   STANDBY GO_2PC                        cc cc nn nn d1 d2 d3 d4 Q_number [00 Q_list [00 Q_path]]
 *   STANDING_BY                           cc cc nn nn hr mn sc fr (ff|st) [cue fields]
 *   COMPLETE                              cc cc nn nn [cue fields]
 *   CANCEL                                cc cc nn nn Q_number [00 Q_list [00 Q_path]]
 *   CANCELLED ABORT                       cc cc s1 s2 nn nn
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "msc_decode.h"
#include "midi_defs.h"

/* ------------------------------------------------------------------------ */
/* Tables                                                                    */
/* ------------------------------------------------------------------------ */
typedef struct { uint8_t code; char abbr[5]; const char *name; } fmt_info_t;

/* MSC 1.1.1 section 4.1, exactly. */
static const fmt_info_t FMTS[] = {
    { 0x01, "LTG",  "Lighting (General Category)" },
    { 0x02, "MLT",  "Moving Lights" },
    { 0x03, "CCHG", "Color Changers" },
    { 0x04, "STRB", "Strobes" },
    { 0x05, "LASR", "Lasers" },
    { 0x06, "CHSR", "Chasers" },
    { 0x10, "SND",  "Sound (General Category)" },
    { 0x11, "MUS",  "Music" },
    { 0x12, "CD",   "CD Players" },
    { 0x13, "EPRM", "EPROM Playback" },
    { 0x14, "ATAP", "Audio Tape Machines" },
    { 0x15, "ICOM", "Intercoms" },
    { 0x16, "AMP",  "Amplifiers" },
    { 0x17, "AFX",  "Audio Effects Devices" },
    { 0x18, "EQ",   "Equalizers" },
    { 0x20, "MACH", "Machinery (General Cat.)" },
    { 0x21, "RIG",  "Rigging" },
    { 0x22, "FLY",  "Flys" },
    { 0x23, "LIFT", "Lifts" },
    { 0x24, "TTBL", "Turntables" },
    { 0x25, "TRUS", "Trusses" },
    { 0x26, "ROBO", "Robots" },
    { 0x27, "ANIM", "Animation" },
    { 0x28, "FLOT", "Floats" },
    { 0x29, "BRKW", "Breakaways" },
    { 0x2A, "BARG", "Barges" },
    { 0x30, "VID",  "Video (General Category)" },
    { 0x31, "VTAP", "Video Tape Machines" },
    { 0x32, "VCAS", "Video Cassette Machines" },
    { 0x33, "VDSC", "Video Disc Players" },
    { 0x34, "VSW",  "Video Switchers" },
    { 0x35, "VFX",  "Video Effects" },
    { 0x36, "VCG",  "Video Character Generators" },
    { 0x37, "VSTL", "Video Still Stores" },
    { 0x38, "VMON", "Video Monitors" },
    { 0x40, "PROJ", "Projection (General)" },
    { 0x41, "FILM", "Film Projectors" },
    { 0x42, "SLID", "Slide Projectors" },
    { 0x43, "VPRJ", "Video Projectors" },
    { 0x44, "DISS", "Dissolvers" },
    { 0x45, "SHUT", "Shutter Controls" },
    { 0x50, "PROC", "Process Control (Gen.)" },
    { 0x51, "HYD",  "Hydraulic Oil" },
    { 0x52, "H2O",  "H2O" },
    { 0x53, "CO2",  "CO2" },
    { 0x54, "AIR",  "Compressed Air" },
    { 0x55, "GAS",  "Natural Gas" },
    { 0x56, "FOG",  "Fog" },
    { 0x57, "SMK",  "Smoke" },
    { 0x58, "HAZE", "Cracked Haze" },
    { 0x60, "PYRO", "Pyro (General Category)" },
    { 0x61, "FWRK", "Fireworks" },
    { 0x62, "EXPL", "Explosions" },
    { 0x63, "FLAM", "Flame" },
    { 0x64, "SPOT", "Smoke pots" },
    { 0x7F, "ALL",  "All-types" },
};

typedef enum {
    K_CUE, K_CUE_REQ, K_TIMED, K_SET, K_FIRE, K_NONE,
    K_LIST_OPT, K_LIST_REQ, K_PATH_REQ, K_SETCLK,
    K_2PC_SBY, K_2PC_SBYING, K_2PC_CMPL, K_2PC_CANCEL, K_2PC_STAT
} kind_t;

typedef struct { uint8_t code; uint8_t kind; const char *name; const char *abbr; } cmd_info_t;

/* MSC 1.1.1 sections 4.3 - 4.5 */
static const cmd_info_t CMDS[] = {
    { 0x01, K_CUE,        "GO",             "GO" },
    { 0x02, K_CUE,        "STOP",           "STOP" },
    { 0x03, K_CUE,        "RESUME",         "RESUME" },
    { 0x04, K_TIMED,      "TIMED_GO",       "TGO" },
    { 0x05, K_CUE_REQ,    "LOAD",           "LOAD" },
    { 0x06, K_SET,        "SET",            "SET" },
    { 0x07, K_FIRE,       "FIRE",           "FIRE" },
    { 0x08, K_NONE,       "ALL_OFF",        "ALLOFF" },
    { 0x09, K_NONE,       "RESTORE",        "RESTOR" },
    { 0x0A, K_NONE,       "RESET",          "RESET" },
    { 0x0B, K_CUE,        "GO_OFF",         "GOOFF" },
    { 0x10, K_CUE,        "GO/JAM_CLOCK",   "GOJAM" },
    { 0x11, K_LIST_OPT,   "STANDBY_+",      "SBY+" },
    { 0x12, K_LIST_OPT,   "STANDBY_-",      "SBY-" },
    { 0x13, K_LIST_OPT,   "SEQUENCE_+",     "SEQ+" },
    { 0x14, K_LIST_OPT,   "SEQUENCE_-",     "SEQ-" },
    { 0x15, K_LIST_OPT,   "START_CLOCK",    "CLKGO" },
    { 0x16, K_LIST_OPT,   "STOP_CLOCK",     "CLKSTP" },
    { 0x17, K_LIST_OPT,   "ZERO_CLOCK",     "CLKZRO" },
    { 0x18, K_SETCLK,     "SET_CLOCK",      "CLKSET" },
    { 0x19, K_LIST_OPT,   "MTC_CHASE_ON",   "CHSON" },
    { 0x1A, K_LIST_OPT,   "MTC_CHASE_OFF",  "CHSOFF" },
    { 0x1B, K_LIST_REQ,   "OPEN_CUE_LIST",  "OPNLST" },
    { 0x1C, K_LIST_REQ,   "CLOSE_CUE_LIST", "CLSLST" },
    { 0x1D, K_PATH_REQ,   "OPEN_CUE_PATH",  "OPNPTH" },
    { 0x1E, K_PATH_REQ,   "CLOSE_CUE_PATH", "CLSPTH" },
    { 0x20, K_2PC_SBY,    "STANDBY",        "STBY" },
    { 0x21, K_2PC_SBYING, "STANDING_BY",    "STBYNG" },
    { 0x22, K_2PC_SBY,    "GO_2PC",         "GO2PC" },
    { 0x23, K_2PC_CMPL,   "COMPLETE",       "COMPL" },
    { 0x24, K_2PC_CANCEL, "CANCEL",         "CANCEL" },
    { 0x25, K_2PC_STAT,   "CANCELLED",      "CNCLD" },
    { 0x26, K_2PC_STAT,   "ABORT",          "ABORT" },
};

static const cmd_info_t *cmd_info(uint8_t c) {
    for (size_t i = 0; i < sizeof CMDS / sizeof CMDS[0]; i++)
        if (CMDS[i].code == c) return &CMDS[i];
    return NULL;
}

static const fmt_info_t *fmt_info(uint8_t f) {
    for (size_t i = 0; i < sizeof FMTS / sizeof FMTS[0]; i++)
        if (FMTS[i].code == f) return &FMTS[i];
    return NULL;
}

const char *msc_format_name(uint8_t f) { const fmt_info_t *i = fmt_info(f); return i ? i->name : NULL; }
const char *msc_format_abbr(uint8_t f) { const fmt_info_t *i = fmt_info(f); return i ? i->abbr : NULL; }
const char *msc_command_name(uint8_t c) { const cmd_info_t *i = cmd_info(c); return i ? i->name : NULL; }
const char *msc_command_abbr(uint8_t c) { const cmd_info_t *i = cmd_info(c); return i ? i->abbr : NULL; }

typedef struct { uint16_t code; const char *text; } status_t;

/* Section 6.7: CANCELLED status codes */
static const status_t ST_CANCELLED[] = {
    { 0x8004, "completing" },
    { 0x8008, "paused" },
    { 0x800C, "terminated" },
    { 0x8010, "reversed" },
    { 0x8024, "not standing by" },
    { 0x8028, "manual override in progress" },
};

/* Section 6.7: ABORT status codes (command_format independent) */
static const status_t ST_ABORT[] = {
    { 0x0000, "unknown/undefined error" },
    { 0x8000, "checksum error" },
    { 0x8020, "timeout (controller internal)" },
    { 0x8024, "not standing by" },
    { 0x8028, "manual override initiated" },
    { 0x8030, "manual override in progress" },
    { 0x8040, "deadman interlock not established" },
    { 0x8044, "required safety interlock not established" },
    { 0x8050, "unknown Q_number" },
    { 0x8054, "unknown Q_list" },
    { 0x8058, "unknown Q_path" },
    { 0x805C, "too many cues active" },
    { 0x8060, "cue out of sequence" },
    { 0x8064, "invalid d1 cue data value" },
    { 0x8068, "invalid d2 cue data value" },
    { 0x806C, "invalid d3 cue data value" },
    { 0x8070, "invalid d4 cue data value" },
    { 0x8090, "manual cueing of playback medium required" },
    { 0x80A0, "power failure in controlled device subsystem" },
    { 0x80B0, "reading new show cues from disk" },
};

/* Section 6.7: command_format dependent codes, by format high nibble */
static const status_t ST_LTG[] = {
    { 0x1004, "position motor failure" }, { 0x1008, "scroller motor failure" },
    { 0x100C, "strobe not charged" }, { 0x1010, "laser safety interlock not established" },
};
static const status_t ST_SND[] = {
    { 0x1004, "amplifier failure" }, { 0x1008, "amplifier overload" },
};
static const status_t ST_MACH[] = {
    { 0x1004, "motor failure" }, { 0x1008, "limit switch inhibiting movement" },
    { 0x100C, "unequal movement in multiple section system" }, { 0x1010, "servo failure" },
};
static const status_t ST_VID[] = {
    { 0x1004, "sync lost" }, { 0x1008, "time code lost" },
};
static const status_t ST_PROJ[] = {
    { 0x1004, "film tension lost" }, { 0x1008, "lamp failure" },
};
static const status_t ST_PROC[] = {
    { 0x1004, "hydraulic oil low" }, { 0x1008, "water low" }, { 0x100C, "carbon dioxide low" },
    { 0x1010, "excess gas detected" }, { 0x1014, "gas pilot out" },
    { 0x1018, "improper gas ignition conditions (windy)" }, { 0x101C, "smoke/fog fluid low" },
    { 0x1104, "invalid switch number" }, { 0x1108, "latch setting system inoperative" },
    { 0x1204, "burned out cue light" },
};
static const status_t ST_PYRO[] = {
    { 0x1004, "charge not loaded" }, { 0x1008, "atmospheric conditions prohibit discharge" },
};

#define LOOKUP(tab, code) lookup((tab), sizeof(tab) / sizeof((tab)[0]), (code))
static const char *lookup(const status_t *t, size_t n, uint16_t code) {
    for (size_t i = 0; i < n; i++) if (t[i].code == code) return t[i].text;
    return NULL;
}

const char *msc_status_text(uint8_t cmd, uint8_t fmt, uint16_t st) {
    const char *s = NULL;
    if (cmd == MSC_CANCELLED) s = LOOKUP(ST_CANCELLED, st);
    else s = LOOKUP(ST_ABORT, st);
    if (s) return s;
    if (st >= 0x1000 && st < 0x8000 && cmd == MSC_ABORT) {
        switch (fmt >> 4) {
        case 0: if (fmt >= 0x01) s = LOOKUP(ST_LTG, st); break;
        case 1: s = LOOKUP(ST_SND, st);  break;
        case 2: s = LOOKUP(ST_MACH, st); break;
        case 3: s = LOOKUP(ST_VID, st);  break;
        case 4: s = LOOKUP(ST_PROJ, st); break;
        case 5: s = LOOKUP(ST_PROC, st); break;
        case 6: s = LOOKUP(ST_PYRO, st); break;
        default: break;
        }
        if (s) return s;
    }
    if (st == 0) return "unknown/undefined error";
    if (st < 0x1000) return "manufacturer specific";
    if (st < 0x8000) return "format dependent (undefined)";
    return "undefined status";
}

const char *msc_warn_text(uint32_t bit) {
    switch (bit) {
    case MSC_W_TRUNC:       return "capture truncated";
    case MSC_W_UNTERM:      return "no F7 (unterminated)";
    case MSC_W_TOO_LONG:    return "over 128 bytes";
    case MSC_W_SHORT:       return "data too short";
    case MSC_W_FMT_UNKNOWN: return "unknown cmd format";
    case MSC_W_CMD_UNKNOWN: return "unknown command";
    case MSC_W_EXTENSION:   return "extension set (00)";
    case MSC_W_EXTRA:       return "unexpected data";
    case MSC_W_Q_MISSING:   return "required Q missing";
    case MSC_W_Q_CHARS:     return "non-numeric Q chars";
    case MSC_W_Q_DOTS:      return "'..' in Q number";
    case MSC_W_Q_ORDER:     return "list/path w/o cue";
    case MSC_W_Q_LONG:      return "Q field clipped";
    case MSC_W_TIME:        return "bad time value";
    case MSC_W_CHECKSUM:    return "2PC checksum bad";
    case MSC_W_SEQ0:        return "2PC seq 0 reserved";
    default:                return "?";
    }
}

const char *msc_tc_rate_name(uint8_t type) {
    static const char *const R[4] = { "24", "25", "30DF", "30" };
    return R[type & 3u];
}

unsigned msc_tc_fps(uint8_t type) {
    static const uint8_t F[4] = { 24, 25, 30, 30 };
    return F[type & 3u];
}

void msc_device_short(uint8_t dev, char *buf, size_t n) {
    if (dev == MSC_DEV_ALL) snprintf(buf, n, "ALL");
    else if (dev >= 0x70) snprintf(buf, n, "G%u", (unsigned)(dev - 0x6Fu));
    else snprintf(buf, n, "%u", (unsigned)dev);
}

void msc_device_long(uint8_t dev, char *buf, size_t n) {
    if (dev == MSC_DEV_ALL) snprintf(buf, n, "7Fh All-call");
    else if (dev >= 0x70) snprintf(buf, n, "%02Xh Group %u", (unsigned)dev, (unsigned)(dev - 0x6Fu));
    else snprintf(buf, n, "%02Xh (%u)", (unsigned)dev, (unsigned)dev);
}

void msc_time_str(const msc_time_t *t, bool frames, bool sub, char *buf, size_t n) {
    const char *sg = t->negative ? "-" : "";
    if (!frames)
        snprintf(buf, n, "%s%02u:%02u:%02u", sg, (unsigned)t->hours, (unsigned)t->minutes,
                 (unsigned)t->seconds);
    else if (sub && !t->is_status)
        snprintf(buf, n, "%s%02u:%02u:%02u:%02u.%02u", sg, (unsigned)t->hours, (unsigned)t->minutes,
                 (unsigned)t->seconds, (unsigned)t->frames, (unsigned)t->subframes);
    else
        snprintf(buf, n, "%s%02u:%02u:%02u:%02u", sg, (unsigned)t->hours, (unsigned)t->minutes,
                 (unsigned)t->seconds, (unsigned)t->frames);
}

/* ------------------------------------------------------------------------ */
/* Decoder                                                                   */
/* ------------------------------------------------------------------------ */
bool msc_is_msc(const uint8_t *sx, size_t n) {
    return n >= 4 && sx[0] == MIDI_SYSEX && sx[1] == 0x7F && sx[3] == MSC_SUB_ID;
}

static void decode_time(const uint8_t *p, msc_time_t *t, uint32_t *warn, bool need_ff) {
    memcpy(t->raw, p, 5);
    t->present      = true;
    t->type         = (uint8_t)((p[0] >> 5) & 3u);
    t->hours        = p[0] & 0x1Fu;
    t->colour_frame = (p[1] & 0x40u) != 0;
    t->minutes      = p[1] & 0x3Fu;
    t->seconds      = p[2] & 0x3Fu;
    t->negative     = (p[3] & 0x40u) != 0;
    t->is_status    = (p[3] & 0x20u) != 0;
    t->frames       = p[3] & 0x1Fu;
    bool bad = (p[2] & 0x40u) != 0;                 /* k: reserved, must be 0 */
    if (t->is_status) {
        t->status = p[4];
        if (p[4] & 0x0Fu) bad = true;                /* xxxx reserved */
        if (need_ff) bad = true;                     /* TIMED_GO / SET use {ff} */
    } else {
        t->subframes = p[4];
        if (p[4] > 99u) bad = true;
    }
    if (t->hours > 23u || t->minutes > 59u || t->seconds > 59u || t->frames >= msc_tc_fps(t->type))
        bad = true;
    if (bad) *warn |= MSC_W_TIME;
}

static void q_store(msc_msg_t *d, int slot, const uint8_t *p, unsigned n) {
    char *dst = slot == 0 ? d->cue : slot == 1 ? d->list : d->path;
    bool *has = slot == 0 ? &d->has_cue : slot == 1 ? &d->has_list : &d->has_path;
    unsigned k = 0;
    bool prev_dot = false;
    *has = true;
    for (unsigned i = 0; i < n; i++) {
        uint8_t c = p[i];
        bool dot = c == '.';
        if (!dot && (c < '0' || c > '9')) d->warn |= MSC_W_Q_CHARS;
        if (dot && prev_dot) d->warn |= MSC_W_Q_DOTS;
        prev_dot = dot;
        if (k < MSC_FIELD_MAX) dst[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        else d->warn |= MSC_W_Q_LONG;
    }
    dst[k] = 0;
}

/* Split 00-delimited fields into the given slots (0 cue, 1 list, 2 path).
 * Extra delimiters (including just before F7) are allowed; extra non-empty
 * fields are flagged. */
static void parse_q(msc_msg_t *d, const uint8_t *p, unsigned n, const int *slots, unsigned nslots) {
    unsigned fi = 0, start = 0;
    bool gap = false;
    for (unsigned i = 0; i <= n; i++) {
        if (i < n && p[i] != 0x00) continue;
        unsigned len = i - start;
        if (fi < nslots) {
            if (len) {
                if (gap) d->warn |= MSC_W_Q_ORDER;
                q_store(d, slots[fi], p + start, len);
            } else {
                gap = true;
            }
        } else if (len) {
            d->warn |= MSC_W_EXTRA;
        }
        fi++;
        start = i + 1;
    }
}

static const int SLOTS_CUE[3] = { 0, 1, 2 };
static const int SLOT_LIST[1] = { 1 };
static const int SLOT_PATH[1] = { 2 };

static uint16_t u14(const uint8_t *p) { return (uint16_t)((p[0] & 0x7Fu) | ((unsigned)(p[1] & 0x7Fu) << 7)); }

uint16_t msc_checksum(const uint8_t *sx, size_t end, bool alt) {
    uint16_t sum = 0;
    for (size_t i = 4; i < end; i += 2) {
        unsigned a = (i == 6 || i == 7) ? 0u : sx[i];
        unsigned b = 0;
        if (i + 1 < end) b = (i + 1 == 6 || i + 1 == 7) ? 0u : sx[i + 1];
        sum = (uint16_t)(sum + (alt ? ((a << 8) | b) : (a | (b << 8))));
    }
    sum = (uint16_t)(sum + sx[2]);
    return (uint16_t)(sum & 0x7F7Fu);
}

bool msc_decode(const uint8_t *sx, size_t n, size_t total_len, msc_msg_t *d) {
    if (!sx || !d || !msc_is_msc(sx, n)) return false;
    memset(d, 0, sizeof *d);
    if (total_len < n) total_len = n;
    d->len = total_len > 0xFFFFu ? 0xFFFFu : (uint16_t)total_len;
    d->dev = sx[2];

    bool truncated = total_len > n;
    bool term = !truncated && sx[n - 1] == MIDI_EOX;
    if (truncated) d->warn |= MSC_W_TRUNC;
    else if (!term) d->warn |= MSC_W_UNTERM;
    if (total_len > MSC_MAX_LEN) d->warn |= MSC_W_TOO_LONG;

    size_t end = term ? n - 1 : n;
    if (end <= 4) { d->warn |= MSC_W_SHORT; return true; }
    d->has_fmt = true;
    d->fmt = sx[4];
    if (d->fmt == 0x00) d->warn |= MSC_W_EXTENSION;
    else if (!fmt_info(d->fmt)) d->warn |= MSC_W_FMT_UNKNOWN;
    if (end <= 5) { d->warn |= MSC_W_SHORT; return true; }
    d->has_cmd = true;
    d->cmd = sx[5];
    if (d->fmt == 0x00) return true;               /* extension: no defined layout */
    if (d->cmd == 0x00) { d->warn |= MSC_W_EXTENSION; return true; }
    const cmd_info_t *ci = cmd_info(d->cmd);
    if (!ci) { d->warn |= MSC_W_CMD_UNKNOWN; return true; }

    const uint8_t *p = sx + 6;
    unsigned len = (unsigned)(end - 6);

    switch ((kind_t)ci->kind) {
    case K_CUE:
        parse_q(d, p, len, SLOTS_CUE, 3);
        break;
    case K_CUE_REQ:
        parse_q(d, p, len, SLOTS_CUE, 3);
        if (!d->has_cue) d->warn |= MSC_W_Q_MISSING;
        break;
    case K_TIMED:
        if (len < 5) { d->warn |= MSC_W_SHORT; break; }
        decode_time(p, &d->time, &d->warn, true);
        parse_q(d, p + 5, len - 5, SLOTS_CUE, 3);
        break;
    case K_SET:
        if (len < 4) { d->warn |= MSC_W_SHORT; break; }
        d->has_ctrl = true;
        d->ctrl = u14(p);
        d->value = u14(p + 2);
        if (len >= 9) decode_time(p + 4, &d->time, &d->warn, true);
        if (len > 9) d->warn |= MSC_W_EXTRA;
        else if (len != 4 && len != 9) d->warn |= MSC_W_SHORT;   /* partial time */
        break;
    case K_FIRE:
        if (len < 1) { d->warn |= MSC_W_SHORT; break; }
        d->has_macro = true;
        d->macro = p[0];
        if (len > 1) d->warn |= MSC_W_EXTRA;
        break;
    case K_NONE:
        if (len) d->warn |= MSC_W_EXTRA;
        break;
    case K_LIST_OPT:
        parse_q(d, p, len, SLOT_LIST, 1);
        break;
    case K_LIST_REQ:
        parse_q(d, p, len, SLOT_LIST, 1);
        if (!d->has_list) d->warn |= MSC_W_Q_MISSING;
        break;
    case K_PATH_REQ:
        parse_q(d, p, len, SLOT_PATH, 1);
        if (!d->has_path) d->warn |= MSC_W_Q_MISSING;
        break;
    case K_SETCLK:
        if (len < 5) { d->warn |= MSC_W_SHORT; break; }
        decode_time(p, &d->time, &d->warn, false);
        parse_q(d, p + 5, len - 5, SLOT_LIST, 1);
        break;
    case K_2PC_SBY:
    case K_2PC_SBYING:
    case K_2PC_CMPL:
    case K_2PC_CANCEL:
    case K_2PC_STAT: {
        d->is_2pc = true;
        if (len >= 2) { d->has_chk = true; d->chk_rx = (uint16_t)(p[0] | (p[1] << 8)); }
        unsigned need;
        switch ((kind_t)ci->kind) {
        case K_2PC_SBY:    need = 8; break;
        case K_2PC_SBYING: need = 9; break;
        case K_2PC_STAT:   need = 6; break;
        default:           need = 4; break;
        }
        if (ci->kind == K_2PC_STAT) {
            if (len >= 4) { d->has_status = true; d->status = (uint16_t)(p[2] * 4u + p[3] * 512u); }
            if (len >= 6) { d->has_seq = true; d->seq = u14(p + 4); }
            if (len > 6) d->warn |= MSC_W_EXTRA;
        } else {
            if (len >= 4) { d->has_seq = true; d->seq = u14(p + 2); }
            if (ci->kind == K_2PC_SBY && len >= 8) { d->has_d = true; memcpy(d->d, p + 4, 4); }
            if (ci->kind == K_2PC_SBYING && len >= 9) decode_time(p + 4, &d->time, &d->warn, false);
            if (len >= need) {
                parse_q(d, p + need, len - need, SLOTS_CUE, 3);
                if ((ci->kind == K_2PC_SBY || ci->kind == K_2PC_CANCEL) && !d->has_cue)
                    d->warn |= MSC_W_Q_MISSING;
            }
        }
        if (len < need) d->warn |= MSC_W_SHORT;
        if (d->has_seq && d->seq == 0) d->warn |= MSC_W_SEQ0;
        if (d->has_chk && term) {
            d->chk_checked = true;
            d->chk_calc = msc_checksum(sx, end, false);
            d->chk_ok = d->chk_calc == d->chk_rx;
            if (!d->chk_ok && msc_checksum(sx, end, true) == d->chk_rx) d->chk_ok = d->chk_alt = true;
            if (!d->chk_ok) d->warn |= MSC_W_CHECKSUM;
        }
        break;
    }
    }
    return true;
}

bool msc_decode_midi(const midi_msg_t *m, msc_msg_t *d) {
    if (!m || m->type != MT_SYSEX) return false;
    unsigned have = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
    return msc_decode(m->sx, have, m->sx_len, d);
}

/* ------------------------------------------------------------------------ */
/* Summary line                                                              */
/* ------------------------------------------------------------------------ */
static void cat(char *b, size_t n, const char *fmt, ...) {
    size_t k = strlen(b);
    if (k + 1 >= n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + k, n - k, fmt, ap);
    va_end(ap);
}

static void cat_tok(char *b, size_t n, const char *s) {
    if (!s || !*s) return;
    cat(b, n, "%s%s", b[0] ? " " : "", s);
}

/* detail levels for the summary arguments */
#define A_FULL  0   /* everything */
#define A_SHORT 1   /* no Q_path, time without frames */
#define A_MIN   2   /* Q_number only, no time */

static void cue_part(const msc_msg_t *d, int lv, char *b, size_t n) {
    char t[MSC_FIELD_MAX + 3];
    if (d->has_cue)  { snprintf(t, sizeof t, "Q%s", d->cue);  cat_tok(b, n, t); }
    if (d->has_list && (lv < A_MIN || !d->has_cue)) { snprintf(t, sizeof t, "L%s", d->list); cat_tok(b, n, t); }
    if (d->has_path && (lv == A_FULL || (!d->has_cue && !d->has_list))) {
        snprintf(t, sizeof t, "P%s", d->path); cat_tok(b, n, t);
    }
}

static void time_part(const msc_msg_t *d, int lv, char *b, size_t n) {
    char t[32];
    if (!d->time.present || lv == A_MIN) return;
    msc_time_str(&d->time, lv == A_FULL, false, t, sizeof t);
    cat_tok(b, n, t);
}

static void args_text(const msc_msg_t *d, int lv, char *b, size_t n) {
    char t[32];
    b[0] = 0;
    const cmd_info_t *ci = (d->has_cmd && d->fmt != 0x00) ? cmd_info(d->cmd) : NULL;
    if (!ci) return;
    switch ((kind_t)ci->kind) {
    case K_TIMED:
        cue_part(d, lv, b, n);
        time_part(d, lv, b, n);
        break;
    case K_SET:
        if (d->has_ctrl) { snprintf(t, sizeof t, "%u=%u", (unsigned)d->ctrl, (unsigned)d->value); cat_tok(b, n, t); }
        time_part(d, lv, b, n);
        break;
    case K_FIRE:
        if (d->has_macro) { snprintf(t, sizeof t, "M%u", (unsigned)d->macro); cat_tok(b, n, t); }
        break;
    case K_SETCLK:
        time_part(d, lv == A_MIN ? A_SHORT : lv, b, n);   /* the time is the point */
        if (lv < A_MIN) cue_part(d, lv, b, n);
        break;
    case K_2PC_SBY: case K_2PC_SBYING: case K_2PC_CMPL: case K_2PC_CANCEL: case K_2PC_STAT:
        if (d->has_seq) { snprintf(t, sizeof t, "#%u", (unsigned)d->seq); cat_tok(b, n, t); }
        if (ci->kind == K_2PC_STAT) {
            if (d->has_status) { snprintf(t, sizeof t, "%04Xh", (unsigned)d->status); cat_tok(b, n, t); }
        } else {
            cue_part(d, lv, b, n);
            if (lv == A_FULL) time_part(d, lv, b, n);
        }
        break;
    default:
        cue_part(d, lv, b, n);
        break;
    }
}

void msc_summary(const msc_msg_t *d, unsigned width, char *buf, size_t n) {
    if (!buf || n == 0) return;
    if (width == 0 || width > n - 1) width = (unsigned)(n - 1);
    char fmt[8], cmd[8], dev[8] = "", a[3][96], c[160];
    const char *bang = d->warn ? "!" : "";

    if (!d->has_fmt) snprintf(fmt, sizeof fmt, "?");
    else if (msc_format_abbr(d->fmt)) snprintf(fmt, sizeof fmt, "%s", msc_format_abbr(d->fmt));
    else if (d->fmt == 0x00) snprintf(fmt, sizeof fmt, "EXT");
    else snprintf(fmt, sizeof fmt, "F%02X", (unsigned)d->fmt);

    if (!d->has_cmd) snprintf(cmd, sizeof cmd, "?");
    else if (d->fmt != 0x00 && msc_command_abbr(d->cmd)) snprintf(cmd, sizeof cmd, "%s", msc_command_abbr(d->cmd));
    else snprintf(cmd, sizeof cmd, "C%02X", (unsigned)d->cmd);

    if (d->dev != MSC_DEV_ALL) { dev[0] = '@'; msc_device_short(d->dev, dev + 1, sizeof dev - 1); }
    for (int lv = 0; lv < 3; lv++) args_text(d, lv, a[lv], sizeof a[lv]);

    /* Candidates, most informative first: with device, without device,
     * without format, then shorter arguments (no path / frames), then the
     * minimum (cue only, no time) marked with '~'. */
    static const struct { int8_t lv; bool fmt, dev; } CAND[] = {
        { A_FULL, true, true }, { A_FULL, true, false }, { A_FULL, false, false },
        { A_SHORT, true, false }, { A_SHORT, false, false },
        { A_MIN, true, false }, { A_MIN, false, false },
    };
    for (size_t i = 0; i < sizeof CAND / sizeof CAND[0]; i++) {
        int lv = CAND[i].lv;
        bool cut = lv == A_MIN && strcmp(a[A_MIN], a[A_SHORT]) != 0;
        if (lv == A_MIN && !cut && i == 5) continue;           /* same as A_SHORT */
        c[0] = 0;
        cat(c, sizeof c, "MSC");
        if (!d->has_fmt) cat_tok(c, sizeof c, "?");
        else {
            if (CAND[i].fmt) cat_tok(c, sizeof c, fmt);
            cat_tok(c, sizeof c, cmd);
        }
        cat_tok(c, sizeof c, a[lv]);
        if (CAND[i].dev) cat_tok(c, sizeof c, dev);
        cat(c, sizeof c, "%s%s", cut ? "~" : "", bang);
        if (strlen(c) <= width) { snprintf(buf, n, "%s", c); return; }
    }
    /* still too long: truncate the shortest form, mark with '~' */
    size_t keep = width > 2 ? width - 1 - (d->warn ? 1u : 0u) : width;
    if (keep > strlen(c)) keep = strlen(c);
    c[keep] = 0;
    while (keep > 0 && c[keep - 1] == ' ') c[--keep] = 0;
    if (width > 2) cat(c, sizeof c, "~%s", bang);
    snprintf(buf, n, "%s", c);
}

/* ------------------------------------------------------------------------ */
/* Detail page                                                               */
/* ------------------------------------------------------------------------ */
#define DL_MAXCOL 60

typedef struct {
    detail_line_cb cb;
    void *ctx;
    unsigned cols;
    unsigned count;
} dl_t;

static void dl_emit(dl_t *L, const char *s, msg_class_t cls) {
    char b[DL_MAXCOL + 1];
    snprintf(b, sizeof b, "%s", s);
    if (strlen(b) > L->cols) b[L->cols] = 0;
    if (L->cb) L->cb(b, cls, L->ctx);
    L->count++;
}

/* prefix on the first line, then continuation lines indented by `indent`,
 * breaking at spaces where possible */
static void dl_wrap(dl_t *L, const char *prefix, const char *text, unsigned indent, msg_class_t cls) {
    char line[DL_MAXCOL + 1];
    unsigned plen = (unsigned)strlen(prefix);
    const char *s = text;
    bool first = true;
    do {
        unsigned lead = first ? plen : indent;
        unsigned w = L->cols > lead ? L->cols - lead : 1u;
        unsigned len = (unsigned)strlen(s);
        unsigned take = len;
        if (len > w) {
            take = w;
            for (unsigned i = w; i > 0; i--) if (s[i] == ' ') { take = i; break; }
        }
        if (first) snprintf(line, sizeof line, "%s%.*s", prefix, (int)take, s);
        else snprintf(line, sizeof line, "%*s%.*s", (int)indent, "", (int)take, s);
        dl_emit(L, line, cls);
        s += take;
        while (*s == ' ') s++;
        first = false;
    } while (*s);
}

static void dl_kv(dl_t *L, const char *label, msg_class_t cls, const char *fmt, ...) {
    char v[160], pre[8];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof v, fmt, ap);
    va_end(ap);
    snprintf(pre, sizeof pre, "%-4s ", label);
    dl_wrap(L, pre, v, 5, cls);
}

static void dl_hex(dl_t *L, const uint8_t *b, unsigned n) {
    unsigned per = (L->cols + 1u) / 3u;
    if (per == 0) per = 1;
    char line[DL_MAXCOL + 1];
    for (unsigned i = 0; i < n; i += per) {
        line[0] = 0;
        for (unsigned k = i; k < n && k < i + per; k++)
            cat(line, sizeof line, k == i ? "%02X" : " %02X", (unsigned)b[k]);
        dl_emit(L, line, CLS_NORMAL);
    }
}

static void lighting_ctrl_desc(uint16_t c, char *b, size_t n) {
    b[0] = 0;
    if (c <= 127) snprintf(b, n, "sub master %u", (unsigned)c + 1u);
    else if (c <= 191) snprintf(b, n, "playback %u master %c", (unsigned)(c - 128u) / 2u + 1u, (c & 1u) ? 'B' : 'A');
    else if (c <= 223) snprintf(b, n, "playback %u speed", (unsigned)c - 191u);
    else if (c <= 255) snprintf(b, n, "chase %u master", (unsigned)c - 223u);
    else if (c <= 287) snprintf(b, n, "chase %u speed", (unsigned)c - 255u);
    else if (c == 510) snprintf(b, n, "grand master");
    else if (c == 511) snprintf(b, n, "general fade speed");
    else if (c >= 512 && c <= 1023) snprintf(b, n, "channel %u level", (unsigned)c - 511u);
}

static void detail_time(dl_t *L, const char *label, const msc_time_t *t) {
    char ts[24];
    msc_time_str(t, true, true, ts, sizeof ts);
    dl_kv(L, label, CLS_SYSEX, "%s", ts);
    static const char *const R[4] = { "24 fps", "25 fps", "30 drop-frame", "30 fps" };
    dl_kv(L, "Rate", CLS_SYSEX, "%s%s", R[t->type & 3u], t->colour_frame ? ", colour frame" : "");
    if (t->is_status) {
        char st[48] = "";
        if (t->status & MSC_ST_ESTIMATED) cat_tok(st, sizeof st, "estimated");
        if (t->status & MSC_ST_INVALID)   cat_tok(st, sizeof st, "invalid");
        if (t->status & MSC_ST_FIELD)     cat_tok(st, sizeof st, "field-1");
        dl_kv(L, "TCst", CLS_SYSEX, "%s", st[0] ? st : "normal");
    }
}

static void detail_msc(dl_t *L, const msc_msg_t *d) {
    char t[48];
    msc_device_long(d->dev, t, sizeof t);
    dl_kv(L, "Dev", CLS_SYSEX, "%s", t);
    unsigned vw = L->cols > 5 ? L->cols - 5 : 1;

    if (d->has_fmt) {
        const char *ab = msc_format_abbr(d->fmt);
        const char *nm = msc_format_name(d->fmt);
        dl_kv(L, "Fmt", CLS_SYSEX, "%02Xh %s", (unsigned)d->fmt, ab ? ab : "?");
        dl_wrap(L, "  ", nm ? nm : d->fmt == 0x00 ? "extension set" : "(undefined)", 2, CLS_SYSEX);
    } else {
        dl_kv(L, "Fmt", CLS_ERROR, "(missing)");
    }

    const cmd_info_t *ci = NULL;
    if (d->has_cmd) {
        ci = d->fmt != 0x00 ? cmd_info(d->cmd) : NULL;
        const char *nm = ci ? ci->name : d->cmd == 0x00 ? "extension set" : "(undefined)";
        if (strlen(nm) + 4u <= vw) dl_kv(L, "Cmd", CLS_SYSEX, "%02Xh %s", (unsigned)d->cmd, nm);
        else { dl_kv(L, "Cmd", CLS_SYSEX, "%02Xh", (unsigned)d->cmd); dl_wrap(L, "  ", nm, 2, CLS_SYSEX); }
    } else if (d->has_fmt) {
        dl_kv(L, "Cmd", CLS_ERROR, "(missing)");
    }

    if (ci) {
        kind_t k = (kind_t)ci->kind;
        bool cue_kind = k == K_CUE || k == K_CUE_REQ || k == K_TIMED || k == K_2PC_SBY ||
                        k == K_2PC_SBYING || k == K_2PC_CMPL || k == K_2PC_CANCEL;
        bool list_kind = k == K_LIST_OPT || k == K_LIST_REQ || k == K_SETCLK;

        if (d->is_2pc) {
            if (d->has_chk) {
                if (d->chk_checked && d->chk_ok)
                    dl_kv(L, "Chk", CLS_SYSEX, "%04Xh ok%s", (unsigned)d->chk_rx, d->chk_alt ? " (BE)" : "");
                else if (d->chk_checked) {
                    dl_kv(L, "Chk", CLS_ERROR, "%04Xh BAD", (unsigned)d->chk_rx);
                    dl_kv(L, "Calc", CLS_ERROR, "%04Xh", (unsigned)d->chk_calc);
                } else dl_kv(L, "Chk", CLS_SYSEX, "%04Xh (unchecked)", (unsigned)d->chk_rx);
            }
            if (d->has_status) {
                dl_kv(L, "Stat", CLS_SYSEX, "%02X %02X", (unsigned)(d->status >> 8), (unsigned)(d->status & 0xFFu));
                dl_wrap(L, "  ", msc_status_text(d->cmd, d->fmt, d->status), 2, CLS_SYSEX);
            }
            if (d->has_seq) dl_kv(L, "Seq", CLS_SYSEX, "%u", (unsigned)d->seq);
            if (d->has_d) dl_kv(L, "Data", CLS_SYSEX, "%02X %02X %02X %02X",
                                (unsigned)d->d[0], (unsigned)d->d[1], (unsigned)d->d[2], (unsigned)d->d[3]);
        }
        if (d->time.present && k != K_SET)
            detail_time(L, k == K_2PC_SBYING ? "MaxT" : "Time", &d->time);
        if (cue_kind) dl_kv(L, "Cue", CLS_SYSEX, "%s", d->has_cue ? d->cue : "(none)");
        if (d->has_list || list_kind) dl_kv(L, "List", CLS_SYSEX, "%s", d->has_list ? d->list : "(none)");
        if (d->has_path || k == K_PATH_REQ) dl_kv(L, "Path", CLS_SYSEX, "%s", d->has_path ? d->path : "(none)");
        if (d->has_ctrl) {
            dl_kv(L, "Ctrl", CLS_SYSEX, "%u", (unsigned)d->ctrl);
            if (d->fmt >= 0x01 && d->fmt <= 0x0F) {
                lighting_ctrl_desc(d->ctrl, t, sizeof t);
                if (t[0]) dl_wrap(L, "  = ", t, 4, CLS_SYSEX);
            }
            dl_kv(L, "Val", CLS_SYSEX, "%u", (unsigned)d->value);
        }
        if (d->time.present && k == K_SET) detail_time(L, "Time", &d->time);
        if (d->has_macro) dl_kv(L, "Mac", CLS_SYSEX, "%u", (unsigned)d->macro);
    }

    for (unsigned b = 0; b < MSC_W_COUNT; b++)
        if (d->warn & (1u << b)) dl_wrap(L, "! ", msc_warn_text(1u << b), 2, CLS_ERROR);
}

unsigned sysex_detail(const midi_msg_t *m, unsigned cols, detail_line_cb cb, void *ctx) {
    dl_t L = { cb, ctx, cols > DL_MAXCOL ? DL_MAXCOL : cols, 0 };
    if (!m || m->type != MT_SYSEX) return 0;
    unsigned have = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
    const uint8_t *s = m->sx;
    msc_msg_t d;

    if (msc_decode(s, have, m->sx_len, &d)) {
        detail_msc(&L, &d);
    } else {
        dl_kv(&L, "Len", CLS_SYSEX, "%u bytes", (unsigned)m->sx_len);
        if (have >= 2 && (s[1] == 0x7E || s[1] == 0x7F)) {
            dl_kv(&L, "ID", CLS_SYSEX, "%02Xh universal %s", (unsigned)s[1], s[1] == 0x7F ? "RT" : "non-RT");
            if (have >= 3 && s[2] != MIDI_EOX) dl_kv(&L, "Dev", CLS_SYSEX, "%02Xh", (unsigned)s[2]);
            if (have >= 5 && s[3] != MIDI_EOX && s[4] != MIDI_EOX)
                dl_kv(&L, "Sub", CLS_SYSEX, "%02Xh %02Xh", (unsigned)s[3], (unsigned)s[4]);
        } else if (have >= 4 && s[1] == 0x00) {
            dl_kv(&L, "ID", CLS_SYSEX, "00 %02X %02X", (unsigned)s[2], (unsigned)s[3]);
        } else if (have >= 2 && s[1] != MIDI_EOX) {
            dl_kv(&L, "ID", CLS_SYSEX, "%02Xh%s", (unsigned)s[1], s[1] == 0x7D ? " non-commercial" : "");
        }
        if (m->flags & MSG_F_SX_TRUNC) dl_wrap(&L, "! ", "capture truncated", 2, CLS_ERROR);
        if (m->flags & MSG_F_SX_UNTERM) dl_wrap(&L, "! ", "no F7 (unterminated)", 2, CLS_ERROR);
    }

    char h[40];
    if (have < m->sx_len) snprintf(h, sizeof h, "Raw %ub, %u kept", (unsigned)m->sx_len, have);
    else snprintf(h, sizeof h, "Raw %u bytes", (unsigned)m->sx_len);
    dl_emit(&L, h, CLS_DIM);
    dl_hex(&L, s, have);
    return L.count;
}

/* ------------------------------------------------------------------------ */
/* Builder                                                                   */
/* ------------------------------------------------------------------------ */
void msc_time_encode(const msc_time_t *t, uint8_t out[5]) {
    out[0] = (uint8_t)(((t->type & 3u) << 5) | (t->hours & 0x1Fu));
    out[1] = (uint8_t)((t->colour_frame ? 0x40u : 0u) | (t->minutes & 0x3Fu));
    out[2] = (uint8_t)(t->seconds & 0x3Fu);
    out[3] = (uint8_t)((t->negative ? 0x40u : 0u) | (t->is_status ? 0x20u : 0u) | (t->frames & 0x1Fu));
    out[4] = t->is_status ? (uint8_t)(t->status & 0x70u) : (uint8_t)(t->subframes & 0x7Fu);
}

typedef struct { uint8_t *out; size_t n, k; bool ok; } wr_t;

static void wb(wr_t *w, unsigned b) {
    if (w->k < w->n) w->out[w->k++] = (uint8_t)(b & 0x7Fu);
    else w->ok = false;
}
static void w14(wr_t *w, unsigned v) { wb(w, v & 0x7Fu); wb(w, (v >> 7) & 0x7Fu); }
static void wstr(wr_t *w, const char *s) { while (s && *s) wb(w, (uint8_t)*s++); }
static void wtime(wr_t *w, const msc_time_t *t) {
    uint8_t b[5];
    msc_time_t z;
    if (!t) { memset(&z, 0, sizeof z); t = &z; }
    msc_time_encode(t, b);
    for (int i = 0; i < 5; i++) wb(w, b[i]);
}
static bool nonempty(const char *s) { return s && *s; }
static void wcue(wr_t *w, const msc_build_t *b) {
    wstr(w, b->cue);
    if (nonempty(b->list) || nonempty(b->path)) { wb(w, 0); wstr(w, b->list); }
    if (nonempty(b->path)) { wb(w, 0); wstr(w, b->path); }
}

size_t msc_build(const msc_build_t *b, uint8_t *out, size_t n) {
    wr_t w = { out, n, 0, true };
    if (n < 1) return 0;
    out[w.k++] = MIDI_SYSEX;
    wb(&w, 0x7F);
    wb(&w, b->dev);
    wb(&w, MSC_SUB_ID);
    wb(&w, b->fmt);
    wb(&w, b->cmd);
    const cmd_info_t *ci = cmd_info(b->cmd);
    if (ci) {
        switch ((kind_t)ci->kind) {
        case K_CUE: case K_CUE_REQ: wcue(&w, b); break;
        case K_TIMED:  wtime(&w, b->time); wcue(&w, b); break;
        case K_SET:    w14(&w, b->ctrl); w14(&w, b->value); if (b->time) wtime(&w, b->time); break;
        case K_FIRE:   wb(&w, b->macro); break;
        case K_NONE:   break;
        case K_LIST_OPT: case K_LIST_REQ: wstr(&w, b->list); break;
        case K_PATH_REQ: wstr(&w, b->path); break;
        case K_SETCLK: wtime(&w, b->time); wstr(&w, b->list); break;
        case K_2PC_SBY:
            w14(&w, 0); w14(&w, b->seq);
            for (int i = 0; i < 4; i++) wb(&w, b->d[i]);
            wcue(&w, b);
            break;
        case K_2PC_SBYING: w14(&w, 0); w14(&w, b->seq); wtime(&w, b->time); wcue(&w, b); break;
        case K_2PC_CMPL: case K_2PC_CANCEL: w14(&w, 0); w14(&w, b->seq); wcue(&w, b); break;
        case K_2PC_STAT:
            w14(&w, 0);
            wb(&w, (b->status / 4u) & 0x7Fu);
            wb(&w, (b->status / 512u) & 0x7Fu);
            w14(&w, b->seq);
            break;
        }
    }
    size_t end = w.k;
    if (w.k < w.n) out[w.k++] = MIDI_EOX; else w.ok = false;
    if (!w.ok || w.k > MSC_MAX_LEN) return 0;
    if (ci && ci->kind >= K_2PC_SBY) {
        uint16_t c = msc_checksum(out, end, false);
        out[6] = (uint8_t)(c & 0x7Fu);
        out[7] = (uint8_t)((c >> 8) & 0x7Fu);
    }
    return w.k;
}

/* ------------------------------------------------------------------------ */
/* Generator front end                                                       */
/* ------------------------------------------------------------------------ */
static const uint8_t GEN_GENERAL[] = { 0x7F, 0x01, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
static const uint8_t GEN_CMDS[] = {
    MSC_GO, MSC_STOP, MSC_RESUME, MSC_TIMED_GO, MSC_LOAD, MSC_SET, MSC_FIRE,
    MSC_ALL_OFF, MSC_RESTORE, MSC_RESET, MSC_GO_OFF
};
#define NGENERAL (sizeof GEN_GENERAL / sizeof GEN_GENERAL[0])
#define NFMTS (sizeof FMTS / sizeof FMTS[0])

_Static_assert(NFMTS == MSC_GEN_FORMAT_COUNT, "format table size");
_Static_assert(sizeof GEN_CMDS == MSC_GEN_COMMAND_COUNT, "generator command list size");

static bool is_general(uint8_t f) {
    for (size_t i = 0; i < NGENERAL; i++) if (GEN_GENERAL[i] == f) return true;
    return false;
}

unsigned msc_gen_format_count(void) { return (unsigned)NFMTS; }

uint8_t msc_gen_format(unsigned idx) {
    if (idx < NGENERAL) return GEN_GENERAL[idx];
    idx -= (unsigned)NGENERAL;
    for (size_t i = 0; i < NFMTS; i++) {
        if (is_general(FMTS[i].code)) continue;
        if (idx-- == 0) return FMTS[i].code;
    }
    return MSC_FMT_ALL;
}

unsigned msc_gen_command_count(void) { return (unsigned)(sizeof GEN_CMDS / sizeof GEN_CMDS[0]); }
uint8_t  msc_gen_command(unsigned idx) { return idx < msc_gen_command_count() ? GEN_CMDS[idx] : MSC_GO; }

void msc_gen_cue_text(uint16_t ci, uint8_t cd, char *buf, size_t n) {
    if (ci == 0 && cd == 0) buf[0] = 0;
    else if (cd == 0) snprintf(buf, n, "%u", (unsigned)ci);
    else snprintf(buf, n, "%u.%u", (unsigned)ci, (unsigned)cd);
}

size_t msc_gen_build(const msc_gen_t *g, uint8_t *out, size_t n) {
    char cue[12], list[6] = "";
    msc_gen_cue_text(g->cue_int, g->cue_dec, cue, sizeof cue);
    if (cue[0] && g->list) snprintf(list, sizeof list, "%u", (unsigned)g->list);
    if (g->cmd == MSC_LOAD && !cue[0]) return 0;           /* Q_number required */

    msc_time_t t;
    memset(&t, 0, sizeof t);
    t.present = true;
    t.type = MSC_TC_25;
    unsigned s = g->time_s;
    t.hours = (uint8_t)(s / 3600u > 23u ? 23u : s / 3600u);
    t.minutes = (uint8_t)((s / 60u) % 60u);
    t.seconds = (uint8_t)(s % 60u);

    msc_build_t b;
    memset(&b, 0, sizeof b);
    b.dev = g->dev & 0x7Fu;
    b.fmt = g->fmt & 0x7Fu;
    b.cmd = g->cmd & 0x7Fu;
    b.cue = cue;
    b.list = list;
    b.ctrl = g->ctrl & 0x3FFFu;
    b.value = g->value & 0x3FFFu;
    b.macro = g->macro & 0x7Fu;
    if (g->cmd == MSC_TIMED_GO || (g->cmd == MSC_SET && g->time_s)) b.time = &t;
    return msc_build(&b, out, n);
}
