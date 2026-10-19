/*
 * Host tests for the v0.4 modules: calendar helpers, MTC reader, monitor
 * filters, value wrap-around, seven-segment digits, and the flash log format
 * (sessions, wrap-around, power loss mid-page, CRC errors, large volumes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "timeutil.h"
#include "mtc.h"
#include "msg_filter.h"
#include "valedit.h"
#include "bigdigits.h"
#include "log_store.h"
#include "log_flash_ram.h"
#include "midi_parser.h"
#include "midi_defs.h"
#include "test_common.h"

/* ------------------------------------------------------------------------ */
static void test_timeutil(void) {
    printf("timeutil\n");
    tu_datetime_t d = { 2000, 1, 1, 0, 0, 0 };
    CHECK(tu_to_secs(&d) == 0);
    CHECK(tu_weekday(0) == 6);                       /* Saturday */
    d = (tu_datetime_t){ 2026, 10, 19, 14, 3, 22 };
    uint32_t s = tu_to_secs(&d);
    tu_datetime_t e;
    tu_from_secs(s, &e);
    CHECK(memcmp(&d, &e, sizeof d) == 0);
    CHECK(tu_weekday(s) == 1);                       /* Monday */
    char b[24];
    tu_fmt_date(s, b, sizeof b);  CHECK_STR(b, "2026-10-19");
    tu_fmt_tod(s, 7, true, b, sizeof b); CHECK_STR(b, "14:03:22.007");
    tu_fmt_tod(s, 7, false, b, sizeof b); CHECK_STR(b, "14:03:22");
    tu_fmt_duration(3600 * 27 + 61, b, sizeof b); CHECK_STR(b, "27:01:01");
    CHECK(tu_days_in_month(2024, 2) == 29 && tu_days_in_month(2100 - 1, 2) == 28 && tu_days_in_month(2000, 2) == 29);
    d = (tu_datetime_t){ 2023, 2, 29, 0, 0, 0 };
    CHECK(!tu_valid(&d));
    /* round trip every day 2000..2099 at a few times of day */
    int bad = 0;
    uint32_t prev = 0;
    for (uint16_t y = 2000; y <= 2099; y++)
        for (uint8_t m = 1; m <= 12; m++)
            for (uint8_t dd = 1; dd <= tu_days_in_month(y, m); dd++) {
                tu_datetime_t x = { y, m, dd, (uint8_t)(dd % 24), (uint8_t)(m * 4), (uint8_t)(y % 60) };
                uint32_t v = tu_to_secs(&x);
                tu_datetime_t z;
                tu_from_secs(v, &z);
                if (memcmp(&x, &z, sizeof x) != 0 || (prev && v <= prev)) bad++;
                prev = v;
            }
    CHECK(bad == 0);
    CHECK(tu_to_bcd(59) == 0x59 && tu_from_bcd(0x47) == 47);
    CHECK(tu_parse_build("Oct 19 2026", "14:03:22", &e) && e.year == 2026 && e.month == 10 && e.day == 19 && e.sec == 22);
    CHECK(tu_parse_build("Jan  5 2027", "01:02:03", &e) && e.day == 5 && e.month == 1);
}

/* ------------------------------------------------------------------------ */
/* MTC: a transmitter model. Forward: piece 0 at frame start, the set
 * describes the frame when piece 0 went out. Reverse: pieces 7..0, the set
 * describes the frame when piece 7 went out. */
static void send_set(mtc_t *m, const mtc_tc_t *t, uint8_t rate, int dir, uint64_t *now, uint32_t qf_us) {
    uint8_t nib[8] = {
        (uint8_t)(t->f & 15), (uint8_t)(t->f >> 4), (uint8_t)(t->s & 15), (uint8_t)(t->s >> 4),
        (uint8_t)(t->m & 15), (uint8_t)(t->m >> 4), (uint8_t)(t->h & 15),
        (uint8_t)(((t->h >> 4) & 1) | (rate << 1)),
    };
    for (int k = 0; k < 8; k++) {
        int p = dir > 0 ? k : 7 - k;
        mtc_quarter_frame(m, (uint8_t)(p << 4 | nib[p]), *now);
        *now += qf_us;
    }
}

static int tc_cmp(const mtc_tc_t *a, const mtc_tc_t *b) {
    return a->h == b->h && a->m == b->m && a->s == b->s && a->f == b->f;
}

static void test_mtc(void) {
    printf("mtc\n");
    char b[16];
    mtc_tc_t t = { 1, 0, 59, 29 };
    mtc_tc_inc(&t, MTC_RATE_2997);
    CHECK(t.h == 1 && t.m == 1 && t.s == 0 && t.f == 2);      /* frames 0,1 dropped at minute 1 */
    mtc_tc_dec(&t, MTC_RATE_2997);
    CHECK(t.m == 0 && t.s == 59 && t.f == 29);
    t = (mtc_tc_t){ 1, 9, 59, 29 };
    mtc_tc_inc(&t, MTC_RATE_2997);
    CHECK(t.m == 10 && t.s == 0 && t.f == 0);                 /* not dropped at minute 10 */
    t = (mtc_tc_t){ 23, 59, 59, 24 };
    mtc_tc_inc(&t, MTC_RATE_25);
    CHECK(t.h == 0 && t.m == 0 && t.s == 0 && t.f == 0);
    mtc_tc_dec(&t, MTC_RATE_25);
    CHECK(t.h == 23 && t.f == 24);
    mtc_tc_str(&(mtc_tc_t){ 1, 2, 3, 4 }, MTC_RATE_2997, b, sizeof b); CHECK_STR(b, "01:02:03;04");
    mtc_tc_str(&(mtc_tc_t){ 1, 2, 3, 4 }, MTC_RATE_25, b, sizeof b);   CHECK_STR(b, "01:02:03:04");
    /* one hour of 29.97 df frames: 107,892 frames per hour */
    t = (mtc_tc_t){ 0, 0, 0, 0 };
    unsigned n = 0;
    while (!(t.h == 1 && t.m == 0 && t.s == 0 && t.f == 0)) { mtc_tc_inc(&t, MTC_RATE_2997); if (++n >= 200000) break; }
    CHECK(n == 107892);

    static const uint8_t rates[4] = { MTC_RATE_24, MTC_RATE_25, MTC_RATE_2997, MTC_RATE_30 };
    for (int r = 0; r < 4; r++) {
        for (int dir = 1; dir >= -1; dir -= 2) {
            mtc_t m;
            mtc_init(&m);
            uint8_t rate = rates[r];
            mtc_tc_t pos = { 1, dir > 0 ? 0 : 10, 59, 20 };   /* transmitter frame at piece 0 / 7 */
            uint64_t now = 1000;
            uint32_t qf_us = 1000000u / (mtc_fps(rate) * 4u);
            /* start mid-set: half a set first, which must not be accepted */
            for (int k = 4; k < 8; k++) {
                int p = dir > 0 ? k : 7 - k;
                mtc_quarter_frame(&m, (uint8_t)(p << 4), now);
                now += qf_us;
            }
            CHECK(!m.valid);
            int bad = 0, sets = 0;
            for (int set = 0; set < 600; set++) {
                /* the receiver after each quarter frame should show the
                 * transmitter's current frame (+1 quarter, the convention) */
                send_set(&m, &pos, rate, dir, &now, qf_us);
                sets++;
                mtc_tc_t next = pos;
                if (dir > 0) { mtc_tc_inc(&next, rate); mtc_tc_inc(&next, rate); }
                else { mtc_tc_dec(&next, rate); mtc_tc_dec(&next, rate); }
                if (!m.valid || !tc_cmp(&m.tc, &next) || m.rate != rate || m.dir != dir) bad++;
                pos = next;
            }
            CHECK(bad == 0);
            CHECK(m.breaks == 0);   /* the half set at the start is not a break, just not accepted */
            CHECK(mtc_running(&m, now) && !mtc_running(&m, now + MTC_RUN_TIMEOUT_US));
            /* quarter-frame interpolation: frame advances every 4 pieces */
            mtc_tc_t before = m.tc;
            if (dir < 0) mtc_tc_dec(&before, rate);   /* reverse: a quarter back is the previous frame */
            uint8_t nib0 = (uint8_t)(pos.f & 15);
            mtc_quarter_frame(&m, (uint8_t)((dir > 0 ? 0 : 7) << 4 | (dir > 0 ? nib0 : (uint8_t)(((pos.h >> 4) & 1) | (rate << 1)))), now);
            CHECK(tc_cmp(&m.tc, &before));
            (void)sets;
        }
    }
    /* forward, then reverse: needs a full set in the new direction */
    {
        mtc_t m; mtc_init(&m);
        uint64_t now = 1;
        mtc_tc_t p = { 0, 1, 0, 0 };
        send_set(&m, &p, MTC_RATE_25, +1, &now, 10000);
        CHECK(m.valid && m.tc.f == 2 && m.qf_sync && m.dir == 1);
        mtc_quarter_frame(&m, 0x70, now);   /* 7 again: tape stops and turns round */
        CHECK(!m.qf_sync && m.tc.f == 2);   /* holds the last position */
        mtc_init(&m);
        send_set(&m, &p, MTC_RATE_25, +1, &now, 10000);
        p = (mtc_tc_t){ 0, 0, 59, 10 };
        send_set(&m, &p, MTC_RATE_25, -1, &now, 10000);   /* 7 right after 7: a break, then a reverse set */
        CHECK(m.valid && m.qf_sync && m.dir == -1 && m.tc.m == 0 && m.tc.s == 59 && m.tc.f == 8);
    }
    /* a missing piece breaks the set */
    {
        mtc_t m; mtc_init(&m);
        uint64_t now = 1;
        for (int k = 0; k < 8; k++) if (k != 5) mtc_quarter_frame(&m, (uint8_t)(k << 4), now++);
        CHECK(!m.valid);
    }
    /* full frame: F0 7F 7F 01 01 hr mn sc fr F7 */
    {
        mtc_t m; mtc_init(&m);
        uint8_t ff[10] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, (uint8_t)(0x40 | 10), 20, 30, 12, 0xF7 };
        CHECK(mtc_full_frame(&m, ff, 10, 5));
        CHECK(m.valid && !m.qf_sync && m.tc.h == 10 && m.tc.m == 20 && m.tc.s == 30 && m.tc.f == 12 && m.rate == MTC_RATE_2997);
        CHECK(!mtc_running(&m, 6) && mtc_last_us(&m) == 5);
        uint8_t notff[10] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0, 0, 0, 0, 0xF7 };
        CHECK(!mtc_full_frame(&m, notff, 10, 6));
        uint8_t badff[10] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x20, 0, 0, 25, 0xF7 };   /* frame 25 at 25 fps */
        CHECK(mtc_full_frame(&m, badff, 10, 7) && m.tc.h == 10);
    }
}

/* ------------------------------------------------------------------------ */
static midi_msg_t mk(uint8_t st, uint8_t d1, uint8_t d2, uint8_t src) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.status = st; m.d1 = d1; m.d2 = d2; m.src = src;
    m.type = midi_type_of_status(st);
    return m;
}
static midi_msg_t mksx(const uint8_t *b, unsigned n, uint8_t src) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = MT_SYSEX; m.status = 0xF0; m.src = src;
    memcpy(m.sx, b, n);
    m.sx_len = (uint16_t)n;
    return m;
}

static void test_filter(void) {
    printf("filter\n");
    msg_filter_t f;
    msg_filter_defaults(&f);
    CHECK(msg_filter_preset_active(&f, FP_NO_REALTIME));
    midi_msg_t clk = mk(0xF8, 0, 0, 0), qf = mk(0xF1, 0x23, 0, 0), note = mk(0x93, 60, 100, 1);
    midi_msg_t pc = mk(0xC0, 5, 0, 0), start = mk(0xFA, 0, 0, 0), as = mk(0xFE, 0, 0, 0);
    const uint8_t msc[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', 0xF7 };
    const uint8_t msc9[] = { 0xF0, 0x7F, 0x09, 0x02, 0x01, 0x01, '1', 0xF7 };
    const uint8_t mscall[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x01, '1', 0xF7 };
    const uint8_t mmc[] = { 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7 };
    const uint8_t ff[] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x21, 0, 0, 0, 0xF7 };
    const uint8_t roland[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0xF7 };
    midi_msg_t m_msc = mksx(msc, sizeof msc, 0), m_msc9 = mksx(msc9, sizeof msc9, 0), m_all = mksx(mscall, sizeof mscall, 0);
    midi_msg_t m_mmc = mksx(mmc, sizeof mmc, 0), m_ff = mksx(ff, sizeof ff, 0), m_sx = mksx(roland, sizeof roland, 0);
    midi_msg_t err; memset(&err, 0, sizeof err); err.type = MT_ERROR; err.d1 = 1; err.src = 1;
    CHECK(msg_filter_cat(&m_msc) == FC_MSC && msg_filter_cat(&m_mmc) == FC_MMC && msg_filter_cat(&m_ff) == FC_MTC_FULL);
    CHECK(msg_filter_cat(&m_sx) == FC_SYSEX && msg_filter_cat(&qf) == FC_MTC_QF && msg_filter_cat(&start) == FC_TRANSPORT);
    CHECK(!msg_filter_pass(&f, &clk) && !msg_filter_pass(&f, &qf) && !msg_filter_pass(&f, &as));
    CHECK(msg_filter_pass(&f, &note) && msg_filter_pass(&f, &m_msc) && msg_filter_pass(&f, &err) && msg_filter_pass(&f, &start));
    msg_filter_apply_preset(&f, FP_SHOW_CONTROL);
    CHECK(msg_filter_preset_active(&f, FP_SHOW_CONTROL) && !msg_filter_preset_active(&f, FP_ALL));
    CHECK(!msg_filter_pass(&f, &note) && msg_filter_pass(&f, &pc) && msg_filter_pass(&f, &m_mmc) && msg_filter_pass(&f, &m_ff));
    CHECK(!msg_filter_pass(&f, &m_sx) && msg_filter_pass(&f, &start) && msg_filter_pass(&f, &err));
    msg_filter_apply_preset(&f, FP_NONE);
    CHECK(!msg_filter_pass(&f, &m_msc) && msg_filter_pass(&f, &err));   /* errors keep their own toggle */
    f.types &= ~FC_BIT(FC_ERROR);
    CHECK(!msg_filter_pass(&f, &err));
    msg_filter_apply_preset(&f, FP_ALL);
    CHECK(!msg_filter_pass(&f, &err) && msg_filter_pass(&f, &clk) && msg_filter_pass(&f, &qf));
    CHECK(!msg_filter_preset_active(&f, FP_NO_REALTIME));
    /* channel (channel voice only) */
    f.channel = 3;
    CHECK(msg_filter_pass(&f, &note) && !msg_filter_pass(&f, &pc) && msg_filter_pass(&f, &clk) && msg_filter_pass(&f, &m_msc));
    f.channel = -1;
    /* inputs */
    f.inputs = SHOW_IN_A;
    CHECK(!msg_filter_pass(&f, &note) && msg_filter_pass(&f, &pc));
    f.inputs = SHOW_IN_B;
    CHECK(msg_filter_pass(&f, &note) && !msg_filter_pass(&f, &pc));
    f.inputs = SHOW_IN_BOTH;
    /* MSC device filter: device and all-call pass, other devices do not, non-MSC unaffected */
    f.msc_dev = 1;
    CHECK(msg_filter_pass(&f, &m_msc) && msg_filter_pass(&f, &m_all) && !msg_filter_pass(&f, &m_msc9) && msg_filter_pass(&f, &note));
    /* type-only check ignores inputs / channel / device */
    f.inputs = SHOW_IN_A;
    CHECK(msg_filter_type_pass(&f, &note));
    for (int c = 0; c < FC_COUNT; c++) CHECK(strlen(msg_filter_cat_label((filter_cat_t)c)) <= 15);
}

/* ------------------------------------------------------------------------ */
static void test_valedit(void) {
    printf("value wrap\n");
    CHECK(val_wrap_step(127, 1, 0, 127) == 0);
    CHECK(val_wrap_step(0, -1, 0, 127) == 127);
    CHECK(val_wrap_step(5, 1, 0, 127) == 6 && val_wrap_step(5, -1, 0, 127) == 4);
    CHECK(val_wrap_step(16, 1, 1, 16) == 1 && val_wrap_step(1, -1, 1, 16) == 16);   /* channel */
    CHECK(val_wrap_step(-1, -1, -1, 127) == 127 && val_wrap_step(127, 1, -1, 127) == -1);  /* "all" + devices */
    CHECK(val_wrap_step(100, 10, 10, 100) == 10 && val_wrap_step(10, -10, 10, 100) == 100); /* brightness */
    CHECK(val_wrap_step(90, 10, 10, 100) == 100);
    /* a big step stops on the end first, then wraps */
    CHECK(val_wrap_step(16380, 10, 0, 16383) == 16383 && val_wrap_step(16383, 10, 0, 16383) == 0);
    CHECK(val_wrap_step(3, -10, 0, 16383) == 0 && val_wrap_step(0, -10, 0, 16383) == 16383);
    CHECK(val_wrap_step(300, 1, 30, 300) == 30 && val_wrap_step(30, -1, 30, 300) == 300);    /* BPM */
    CHECK(val_wrap_step(55, 1, 0, 55) == 0);                                                  /* MSC formats */
    CHECK(val_wrap_step(7, 0, 0, 5) == 5 && val_wrap_step(3, 1, 3, 3) == 3);
    /* acceleration */
    CHECK(val_accel_step(1, 0, 127, 50) == 1);           /* small ranges never accelerate */
    CHECK(val_accel_step(1, 0, 16383, 0) == 1 && val_accel_step(1, 0, 16383, VAL_ACCEL_N10 - 1) == 1);
    CHECK(val_accel_step(1, 0, 16383, VAL_ACCEL_N10) == 10 && val_accel_step(1, 0, 16383, VAL_ACCEL_N100) == 100);
    CHECK(val_accel_step(1, 0, 3599, VAL_ACCEL_N100) == 10);
    CHECK(val_accel_step(1, 0, 9999, VAL_ACCEL_N100) == 10);
    CHECK(val_accel_step(1, 30, 300, VAL_ACCEL_N10) == 10);
    /* walk the full circle both ways */
    int v = 0, ok = 1;
    for (int i = 0; i < 128; i++) v = val_wrap_step(v, 1, 0, 127);
    ok &= v == 0;
    for (int i = 0; i < 128; i++) v = val_wrap_step(v, -1, 0, 127);
    ok &= v == 0;
    CHECK(ok);
}

static void test_bigdigits(void) {
    printf("big digits\n");
    static uint8_t bits[40 * 96];
    int st = 40;
    memset(bits, 0xFF, sizeof bits);
    bd_glyph(bits, st, 8, 4, 30, 80, 7, '8');
    /* centre column at segment g lit, inside of the upper loop clear */
    CHECK(bd_get(bits, st, 8 + 15, 4 + 39));
    CHECK(!bd_get(bits, st, 8 + 15, 4 + 20));
    bd_glyph(bits, st, 8, 4, 30, 80, 7, '1');
    CHECK(!bd_get(bits, st, 8 + 15, 4 + 39) && bd_get(bits, st, 8 + 26, 4 + 20));
    bd_glyph(bits, st, 8, 4, 30, 80, 7, ' ');
    int lit = 0;
    for (int y = 4; y < 84; y++) for (int x = 8; x < 38; x++) lit += bd_get(bits, st, x, y);
    CHECK(lit == 0);
    CHECK(bd_get(bits, st, 7, 4) && bd_get(bits, st, 38, 4));   /* neighbours untouched */
}

/* ------------------------------------------------------------------------ */
/* Flash log                                                                 */
/* ------------------------------------------------------------------------ */
static uint32_t rng_state = 12345;
static uint32_t rnd(void) { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

static midi_msg_t rand_msg(uint64_t t) {
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.t_us = t;
    m.src = (uint8_t)(rnd() & 1);
    unsigned k = rnd() % 12;
    if (k < 5) {
        static const uint8_t st[] = { 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 };
        uint8_t s = (uint8_t)(st[rnd() % 7] | (rnd() & 15));
        m = mk(s, (uint8_t)(rnd() & 0x7F), midi_data_len(s) > 1 ? (uint8_t)(rnd() & 0x7F) : 0, m.src);
    } else if (k < 7) {
        static const uint8_t rt[] = { 0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF };
        m = mk(rt[rnd() % 6], 0, 0, m.src);
    } else if (k == 7) {
        m = mk(0xF1, (uint8_t)(rnd() & 0x7F), 0, m.src);
    } else if (k == 8) {
        m = mk(0xF2, (uint8_t)(rnd() & 0x7F), (uint8_t)(rnd() & 0x7F), m.src);
    } else if (k == 9) {
        memset(&m, 0, sizeof m);
        m.type = MT_ERROR; m.d1 = (uint8_t)(1 + rnd() % 4); m.src = (uint8_t)(rnd() & 1);
    } else {
        unsigned n = 2 + rnd() % 300;
        uint8_t b[MIDI_SX_STORE];
        unsigned keep = n < MIDI_SX_STORE ? n : MIDI_SX_STORE;
        for (unsigned i = 0; i < keep; i++) b[i] = (uint8_t)(rnd() & 0x7F);
        b[0] = 0xF0;
        bool unterm = (rnd() % 5) == 0;
        if (n <= MIDI_SX_STORE && !unterm) b[n - 1] = 0xF7;
        m = mksx(b, keep, (uint8_t)(rnd() & 1));
        m.sx_len = (uint16_t)n;
        if (n > MIDI_SX_STORE) m.flags |= MSG_F_SX_TRUNC;
        if (unterm) m.flags |= MSG_F_SX_UNTERM;
    }
    m.t_us = t;
    return m;
}

static bool same_msg(const midi_msg_t *a, const midi_msg_t *b) {
    if (a->type != b->type || a->src != b->src || a->t_us != b->t_us) return false;
    if (a->type == MT_ERROR) return a->d1 == b->d1;
    if (a->type == MT_SYSEX) {
        unsigned k = a->sx_len < MIDI_SX_STORE ? a->sx_len : MIDI_SX_STORE;
        return a->sx_len == b->sx_len && a->flags == b->flags && memcmp(a->sx, b->sx, k) == 0;
    }
    return a->status == b->status && a->d1 == b->d1 && a->d2 == b->d2;
}

static void drain(log_t *L, bool erase_ok) {
    for (int i = 0; i < 100000; i++) {
        log_op_t op = log_service(L, erase_ok);
        if (op == LOG_OP_NONE || op == LOG_OP_WAIT) {
            if (log_pending(L) == 0) return;
            if (op == LOG_OP_WAIT) erase_ok = true;
        }
    }
}

static log_time_t mkt(uint64_t us, bool real, uint32_t base_s) {
    log_time_t t = { us, real, 0, 0 };
    if (real) {
        uint64_t ms = (uint64_t)base_s * 1000u + us / 1000u;
        t.real_s = (uint32_t)(ms / 1000u);
        t.real_ms = (uint16_t)(ms % 1000u);
    }
    return t;
}

/* Read a whole session forwards and backwards and compare with expect[]. */
static int check_session(log_t *L, uint32_t no, const midi_msg_t *expect, int n, bool real, uint32_t base_s) {
    int bad = 0, k = 0;
    log_pos_t p;
    midi_msg_t m;
    log_time_t t;
    bool ok = log_pos_first(L, no, &p);
    while (ok) {
        if (k >= n || !log_read(L, &p, &m, &t) || !same_msg(&m, &expect[k])) bad++;
        else if (t.real != real || (real && (t.real_s * 1000ull + t.real_ms) != base_s * 1000ull + expect[k].t_us / 1000u)) bad++;
        k++;
        ok = log_pos_next(L, no, &p);
    }
    if (k != n) bad += 1000;
    k = n - 1;
    ok = log_pos_last(L, no, &p);
    while (ok) {
        if (k < 0 || !log_read(L, &p, &m, NULL) || !same_msg(&m, &expect[k])) bad++;
        k--;
        ok = log_pos_prev(L, no, &p);
    }
    if (k != -1) bad += 1000;
    return bad;
}

static void test_log_basic(void) {
    printf("log: sessions, read back, reboot\n");
    const uint32_t size = 160 * LOG_SECTOR_SIZE;     /* 640 kB */
    uint8_t *mem = malloc(size);
    log_ram_flash_t rf;
    log_flash_t fl;
    log_ram_flash_init(&rf, &fl, mem, size, true);
    static log_t L;
    log_init(&L, &fl);
    CHECK(L.enabled && log_session_count(&L) == 0 && L.used_pages == 0 && L.bad_pages == 0);

    /* session 1: no real time, 3000 messages; session 2: real time, 5000 */
    static midi_msg_t ex1[3000], ex2[5000];
    uint64_t t = 1000;
    log_time_t st = mkt(t, false, 0);
    log_new_session(&L, &st);
    uint32_t s1 = log_session(&L, 0)->no;
    CHECK(log_is_live(&L, s1));
    for (int i = 0; i < 3000; i++) {
        t += rnd() % 30000;
        ex1[i] = rand_msg(t);
        CHECK(log_append(&L, &ex1[i], &(log_time_t){ t, false, 0, 0 }) || 1);
        if (i % 50 == 0) drain(&L, false);
    }
    log_flush(&L);
    drain(&L, true);
    CHECK(log_find(&L, s1)->nrec == 3000);
    CHECK(check_session(&L, s1, ex1, 3000, false, 0) == 0);

    const uint32_t base = 843000000u;     /* 2026-ish */
    st = mkt(t, true, base);
    log_new_session(&L, &st);
    uint32_t s2 = log_session(&L, 0)->no;
    CHECK(s2 == s1 + 1 && log_session_count(&L) == 2 && !log_is_live(&L, s1));
    /* out-of-order timestamps (a long sysex on A finishing after a note on B) */
    for (int i = 0; i < 5000; i++) {
        t += rnd() % 20000;
        ex2[i] = rand_msg((i % 7 == 3) ? t - 15000 : t);
    }
    /* write both sessions on a fresh flash */
    log_ram_flash_init(&rf, &fl, mem, size, true);
    log_init(&L, &fl);
    st = mkt(1000, false, 0);
    log_new_session(&L, &st);
    s1 = log_session(&L, 0)->no;
    for (int i = 0; i < 3000; i++) {
        log_append(&L, &ex1[i], &(log_time_t){ ex1[i].t_us, false, 0, 0 });
        if (i % 30 == 0) drain(&L, false);
    }
    log_flush(&L);
    drain(&L, true);
    st = mkt(t, true, base);
    log_new_session(&L, &st);
    s2 = log_session(&L, 0)->no;
    for (int i = 0; i < 5000; i++) {
        log_time_t tt = mkt(ex2[i].t_us, true, base);
        log_append(&L, &ex2[i], &tt);
        if (i % 40 == 0) drain(&L, false);
    }
    CHECK(log_find(&L, s2)->nrec == 5000);
    log_flush(&L);
    drain(&L, true);
    CHECK(L.drops == 0);
    CHECK(check_session(&L, s1, ex1, 3000, false, 0) == 0);
    CHECK(check_session(&L, s2, ex2, 5000, true, base) == 0);
    const log_sess_t *e2 = log_find(&L, s2);
    CHECK(e2->start.real && e2->start.real_s == st.real_s && e2->has_end && e2->end.real);
    CHECK(e2->end.t_us == ex2[4999].t_us);
    unsigned used = log_used_pct(&L);
    CHECK(used > 0 && used < 100);

    /* reboot: same index from the flash contents */
    static log_t L2;
    log_init(&L2, &fl);
    CHECK(log_session_count(&L2) == 2 && L2.bad_pages == 0);
    CHECK(log_find(&L2, s1)->nrec == 3000 && log_find(&L2, s2)->nrec == 5000);
    CHECK(!log_find(&L2, s1)->partial && log_find(&L2, s2)->start.real_s == st.real_s);
    CHECK(log_find(&L2, s2)->end.t_us == ex2[4999].t_us && log_find(&L2, s2)->end.real);
    CHECK(L2.next_session == s2 + 1 && L2.next_seq == L.next_seq);
    CHECK(check_session(&L2, s2, ex2, 5000, true, base) == 0);
    CHECK(check_session(&L2, s1, ex1, 3000, false, 0) == 0);
    /* a new session after reboot continues the numbering and the ring */
    st = mkt(5, false, 0);
    log_new_session(&L2, &st);
    CHECK(log_session(&L2, 0)->no == s2 + 1 && log_session(&L2, 0)->nrec == 0);
    midi_msg_t one = mk(0x90, 60, 1, 0);
    one.t_us = 77;
    log_append(&L2, &one, &(log_time_t){ 77, false, 0, 0 });
    log_flush(&L2);
    drain(&L2, true);
    CHECK(check_session(&L2, s2 + 1, &one, 1, false, 0) == 0);
    CHECK(check_session(&L2, s2, ex2, 5000, true, base) == 0);
    /* empty session: listed while live, not persisted */
    log_new_session(&L2, &st);
    CHECK(log_session_count(&L2) == 4);
    static log_t L3;
    log_init(&L3, &fl);
    CHECK(log_session_count(&L3) == 3);
    /* clock set mid-page (offset jump) starts a new page with the new mapping */
    st = mkt(10, true, base);
    log_new_session(&L3, &st);
    uint32_t s4 = log_session(&L3, 0)->no;
    midi_msg_t a = mk(0xC0, 1, 0, 0), b = mk(0xC0, 2, 0, 0);
    a.t_us = 2000000; b.t_us = 3000000;
    log_append(&L3, &a, &(log_time_t){ a.t_us, true, base + 2, 0 });
    log_append(&L3, &b, &(log_time_t){ b.t_us, true, base + 3600, 0 });
    log_flush(&L3);
    drain(&L3, true);
    log_pos_t p;
    log_time_t tt;
    midi_msg_t r;
    CHECK(log_pos_last(&L3, s4, &p) && log_read(&L3, &p, &r, &tt) && tt.real_s == base + 3600 && r.d1 == 2);
    CHECK(log_pos_prev(&L3, s4, &p) && log_read(&L3, &p, &r, &tt) && tt.real_s == base + 2 && r.d1 == 1);
    free(mem);
}

static void test_log_wrap(void) {
    printf("log: ring wrap-around\n");
    const uint32_t size = 16 * LOG_SECTOR_SIZE;      /* 64 kB: wraps quickly */
    uint8_t *mem = malloc(size);
    log_ram_flash_t rf;
    log_flash_t fl;
    log_ram_flash_init(&rf, &fl, mem, size, true);
    static log_t L;
    log_init(&L, &fl);
    /* 40 sessions of 400 messages: about 7x the region */
    enum { NS = 40, NM = 400 };
    static midi_msg_t ex[NS][NM];
    uint32_t nos[NS];
    uint64_t t = 0;
    for (int s = 0; s < NS; s++) {
        log_time_t st = mkt(t, false, 0);
        log_new_session(&L, &st);
        nos[s] = log_session(&L, 0)->no;
        for (int i = 0; i < NM; i++) {
            t += 1000 + rnd() % 5000;
            ex[s][i] = rand_msg(t);
            log_append(&L, &ex[s][i], &(log_time_t){ t, false, 0, 0 });
            if (i % 8 == 0) drain(&L, i % 3 == 0);
        }
        log_flush(&L);
        drain(&L, true);
    }
    CHECK(L.drops == 0);
    CHECK(L.erases > 16);
    /* newest sessions intact, oldest gone, the boundary session partial */
    int nsess = log_session_count(&L);
    CHECK(nsess >= 3 && nsess < NS);
    const log_sess_t *oldest = log_session(&L, nsess - 1);
    CHECK(oldest->no > nos[0]);
    int bad = 0;
    for (int s = 0; s < NS; s++) {
        const log_sess_t *e = log_find(&L, nos[s]);
        if (!e) continue;
        if (e->no == oldest->no) {
            /* partial: whatever is left must be the tail of the session, in order */
            log_pos_t p;
            midi_msg_t m;
            int k = NM - (int)e->nrec;
            if (k < 0) { bad++; continue; }
            if (e->nrec < NM && !e->partial) bad++;
            bool ok = log_pos_first(&L, e->no, &p);
            while (ok) {
                if (k >= NM || !log_read(&L, &p, &m, NULL) || !same_msg(&m, &ex[s][k])) bad++;
                k++;
                ok = log_pos_next(&L, e->no, &p);
            }
            if (k != NM) bad++;
        } else {
            if (e->nrec != NM || check_session(&L, e->no, ex[s], NM, false, 0)) bad++;
        }
    }
    CHECK(bad == 0);
    CHECK(log_find(&L, nos[NS - 1]) != NULL);
    /* reboot after wrap: same picture */
    static log_t L2;
    log_init(&L2, &fl);
    CHECK(log_session_count(&L2) == nsess || log_session_count(&L2) == nsess - 1);
    CHECK(L2.bad_pages == 0);
    for (int s = NS - 3; s < NS; s++) CHECK(check_session(&L2, nos[s], ex[s], NM, false, 0) == 0);
    CHECK(L2.next_session == L.next_session && L2.next_seq == L.next_seq);
    /* keeps writing after the reboot (pointer mid-ring) */
    log_time_t st = mkt(t, false, 0);
    log_new_session(&L2, &st);
    uint32_t sn = log_session(&L2, 0)->no;
    for (int i = 0; i < NM; i++) {
        log_append(&L2, &ex[0][i], &(log_time_t){ ex[0][i].t_us, false, 0, 0 });
        drain(&L2, true);
    }
    log_flush(&L2);
    drain(&L2, true);
    CHECK(check_session(&L2, sn, ex[0], NM, false, 0) == 0);
    CHECK(check_session(&L2, nos[NS - 1], ex[NS - 1], NM, false, 0) == 0);

    /* delete all */
    log_wipe_start(&L2);
    unsigned pct = 0;
    int guard = 0;
    while (log_wiping(&L2, &pct) && guard++ < 1000) log_service(&L2, false);
    CHECK(!log_wiping(&L2, NULL) && log_session_count(&L2) == 0 && L2.used_pages == 0);
    int nonblank = 0;
    for (uint32_t i = 0; i < size; i++) nonblank += mem[i] != 0xFF;
    CHECK(nonblank == 0);
    static log_t L3;
    log_init(&L3, &fl);
    CHECK(log_session_count(&L3) == 0);
    free(mem);
}

static void test_log_powerloss(void) {
    printf("log: power loss mid-page, CRC errors\n");
    const uint32_t size = 32 * LOG_SECTOR_SIZE;
    uint8_t *mem = malloc(size);
    log_ram_flash_t rf;
    log_flash_t fl;
    static log_t L;
    static midi_msg_t ex[2000];
    int cut_ok = 0, cut_runs = 0, torn = 0;
    /* cut power during the k-th page program at various byte counts */
    for (int run = 0; run < 24; run++) {
        log_ram_flash_init(&rf, &fl, mem, size, true);
        log_init(&L, &fl);
        log_time_t st = mkt(0, false, 0);
        log_new_session(&L, &st);
        uint32_t no = log_session(&L, 0)->no;
        rf.fail_after_programs = 5 + run * 2;
        rf.fail_bytes = (uint32_t)(run * 11) % LOG_PAGE_SIZE;
        uint64_t t = 0;
        int n = 0;
        for (int i = 0; i < 2000; i++) ex[i] = rand_msg(3000u * (uint64_t)(i + 1));
        for (int i = 0; i < 2000 && rf.powered; i++) {
            t += 3000;
            log_append(&L, &ex[i], &(log_time_t){ t, false, 0, 0 });
            n++;
            drain(&L, true);
        }
        /* "reboot" */
        rf.powered = true;
        rf.fail_after_programs = -1;
        static log_t R;
        log_init(&R, &fl);
        const log_sess_t *e = log_find(&R, no);
        cut_runs++;
        torn += (int)R.bad_pages;
        if (!e) continue;   /* nothing complete (only possible very early) */
        /* everything that was read back is a prefix of what was sent */
        log_pos_t p;
        midi_msg_t m;
        int k = 0, bad = 0;
        bool ok = log_pos_first(&R, no, &p);
        while (ok) {
            if (k >= n || !log_read(&R, &p, &m, NULL) || !same_msg(&m, &ex[k])) bad++;
            k++;
            ok = log_pos_next(&R, no, &p);
        }
        if (bad == 0 && k == (int)e->nrec && k > 0) cut_ok++;
        /* writing continues past the torn page */
        st = mkt(t + 1000, false, 0);
        log_new_session(&R, &st);
        uint32_t no2 = log_session(&R, 0)->no;
        for (int i = 0; i < 300; i++) {
            log_append(&R, &ex[i], &(log_time_t){ ex[i].t_us, false, 0, 0 });
            drain(&R, true);
        }
        log_flush(&R);
        drain(&R, true);
        if (check_session(&R, no2, ex, 300, false, 0) != 0) cut_ok -= 100;
    }
    CHECK(cut_ok == cut_runs);
    CHECK(torn > cut_runs / 2);   /* most cuts leave a torn page (a cut at 0 bytes or past the used length does not) */
    printf("  %d power cuts mid-page: %d torn pages found and skipped\n", cut_runs, torn);

    /* CRC errors: corrupt payload bits of pages in the middle of a session */
    log_ram_flash_init(&rf, &fl, mem, size, true);
    log_init(&L, &fl);
    log_time_t st = mkt(0, false, 0);
    log_new_session(&L, &st);
    uint32_t no = log_session(&L, 0)->no;
    uint64_t t = 0;
    for (int i = 0; i < 2000; i++) {
        t += 2000;
        ex[i] = rand_msg(t);
        log_append(&L, &ex[i], &(log_time_t){ t, false, 0, 0 });
        drain(&L, true);
    }
    log_flush(&L);
    drain(&L, true);
    /* flip one bit in pages 3 and 10, damage the header magic of page 6 */
    mem[3 * LOG_PAGE_SIZE + 100] ^= 0x10;
    mem[10 * LOG_PAGE_SIZE + LOG_HDR_SIZE + 5] ^= 0x01;
    mem[6 * LOG_PAGE_SIZE + 0] = 0x00;
    uint8_t n3 = mem[3 * LOG_PAGE_SIZE + 3], n6 = mem[6 * LOG_PAGE_SIZE + 3], n10 = mem[10 * LOG_PAGE_SIZE + 3];
    static log_t R;
    log_init(&R, &fl);
    CHECK(R.bad_pages == 3);
    const log_sess_t *e = log_find(&R, no);
    CHECK(e && e->nrec == 2000u - n3 - n6 - n10);
    /* reading skips the bad pages; the rest is intact and in order */
    log_pos_t p;
    midi_msg_t m;
    int k = 0, bad = 0, nread = 0;
    bool ok = log_pos_first(&R, no, &p);
    while (ok) {
        if (!log_read(&R, &p, &m, NULL)) { bad++; break; }
        while (k < 2000 && !same_msg(&m, &ex[k])) k++;
        if (k >= 2000) bad++;
        k++;
        nread++;
        ok = log_pos_next(&R, no, &p);
    }
    CHECK(bad == 0 && nread == (int)e->nrec);
    /* the same while reading (pages corrupted after boot) */
    mem[20 * LOG_PAGE_SIZE + LOG_HDR_SIZE + 2] ^= 0x40;
    R.cache.ok = false;
    nread = 0;
    ok = log_pos_last(&R, no, &p);
    while (ok) { nread++; ok = log_pos_prev(&R, no, &p); }
    CHECK(nread == (int)e->nrec - (int)mem[20 * LOG_PAGE_SIZE + 3]);
    /* the writer skips non-blank garbage left mid-sector (a torn page after
     * the newest valid one) and leaves it alone */
    log_ram_flash_init(&rf, &fl, mem, size, true);
    log_init(&L, &fl);
    log_new_session(&L, &st);
    no = log_session(&L, 0)->no;
    for (int i = 0; i < 20; i++) log_append(&L, &ex[i], &(log_time_t){ ex[i].t_us, false, 0, 0 });
    log_flush(&L);
    drain(&L, false);
    uint32_t garbage = L.cur_slot;
    CHECK(garbage % LOG_PAGES_PER_SECTOR != 0);
    memset(mem + garbage * LOG_PAGE_SIZE, 0x5A, 10);
    log_init(&L, &fl);
    CHECK(L.bad_pages == 1 && L.cur_slot == garbage);
    log_new_session(&L, &st);
    uint32_t no2 = log_session(&L, 0)->no;
    for (int i = 0; i < 400; i++) {
        log_append(&L, &ex[i], &(log_time_t){ ex[i].t_us, false, 0, 0 });
        drain(&L, false);
    }
    log_flush(&L);
    drain(&L, false);
    CHECK(check_session(&L, no2, ex, 400, false, 0) == 0);
    CHECK(check_session(&L, no, ex, 20, false, 0) == 0);
    CHECK(mem[garbage * LOG_PAGE_SIZE] == 0x5A);
    free(mem);
}

static void test_log_volume(void) {
    printf("log: volume (100k messages, small queue pressure)\n");
    const uint32_t size = 256 * LOG_SECTOR_SIZE;     /* 1 MB */
    uint8_t *mem = malloc(size);
    log_ram_flash_t rf;
    log_flash_t fl;
    log_ram_flash_init(&rf, &fl, mem, size, true);
    static log_t L;
    log_init(&L, &fl);
    log_time_t st = mkt(0, true, 800000000u);
    log_new_session(&L, &st);
    uint32_t no = log_session(&L, 0)->no;
    enum { N = 100000 };
    midi_msg_t *ex = malloc(N * sizeof *ex);
    uint64_t t = 0;
    int appended = 0;
    for (int i = 0; i < N; i++) {
        t += 320 + rnd() % 2000;
        ex[i] = rand_msg(t);
        log_time_t tt = mkt(t, true, 800000000u);
        if (log_append(&L, &ex[i], &tt)) appended++;
        /* flash keeps up only every 3rd message: the queue absorbs bursts,
         * erases happen when forced */
        if (i % 3 == 0) log_service(&L, false);
    }
    log_flush(&L);
    drain(&L, true);
    const log_sess_t *e = log_find(&L, no);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->nrec <= (uint32_t)appended && e->partial);   /* 1 MB wraps: oldest pages gone */
        printf("  %d messages, %u kept, %u dropped (queue), %u pages, %u erases, %u%% used\n",
               N, (unsigned)e->nrec, (unsigned)L.drops, (unsigned)L.pages_written, (unsigned)L.erases, log_used_pct(&L));
        /* count what can be read back */
        log_pos_t p;
        uint32_t cnt = 0;
        bool ok = log_pos_first(&L, no, &p);
        while (ok) { cnt++; ok = log_pos_next(&L, no, &p); }
        CHECK(cnt == e->nrec);
    }
    free(ex);
    free(mem);
}

void test_m2(void) {
    test_timeutil();
    test_mtc();
    test_filter();
    test_valedit();
    test_bigdigits();
    test_log_basic();
    test_log_wrap();
    test_log_powerloss();
    test_log_volume();
}
