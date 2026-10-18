/*
 * Host-side unit tests for the MIDIum parser, decoder and thru/merge engine.
 * Build and run:  make -C test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "midi_engine.h"
#include "midi_parser.h"
#include "midi_decode.h"
#include "midi_defs.h"

#include "test_common.h"

int g_checks, g_fail;

/* ------------------------------------------------------------------------ */
/* message collector                                                         */
/* ------------------------------------------------------------------------ */
#define MAXMSG 400000
typedef struct { midi_msg_t *m; int n; } msglist_t;

static void collect(const midi_msg_t *m, void *ctx) {
    msglist_t *l = ctx;
    if (l->n < MAXMSG) l->m[l->n++] = *m;
}
static msglist_t ml_new(void) { msglist_t l = { calloc(MAXMSG, sizeof(midi_msg_t)), 0 }; return l; }
static void ml_free(msglist_t *l) { free(l->m); l->m = NULL; l->n = 0; }

static void parse_bytes(const uint8_t *b, int n, msglist_t *out, uint32_t *stray) {
    midi_parser_t p;
    midi_parser_init(&p, collect, out);
    for (int i = 0; i < n; i++) midi_parser_feed(&p, b[i], (uint64_t)i);
    if (stray) *stray = p.stray_data;
}

static bool msg_eq(const midi_msg_t *a, const midi_msg_t *b) {
    if (a->type != b->type || a->status != b->status) return false;
    if (a->type == MT_SYSEX) {
        if (a->sx_len != b->sx_len || a->flags != b->flags) return false;
        unsigned k = a->sx_len < MIDI_SX_STORE ? a->sx_len : MIDI_SX_STORE;
        return memcmp(a->sx, b->sx, k) == 0;
    }
    return a->d1 == b->d1 && a->d2 == b->d2;
}

static void decode(const midi_msg_t *m, char *buf) {
    decode_opts_t o = { false, 0 };
    midi_decode(m, &o, buf, MIDI_TEXT_MAX);
}

/* ------------------------------------------------------------------------ */
/* Parser tests                                                              */
/* ------------------------------------------------------------------------ */
static void test_parser(void) {
    printf("parser\n");
    msglist_t l = ml_new();
    uint32_t stray;
    char t[MIDI_TEXT_MAX];

    /* running status + realtime mid-message */
    const uint8_t s1[] = { 0x90, 0x3C, 0xF8, 0x64, 0x3E, 0x00, 0xFE, 0x40, 0x7F };
    parse_bytes(s1, sizeof s1, &l, &stray);
    CHECK(l.n == 5);
    CHECK(l.m[0].type == MT_CLOCK);
    CHECK(l.m[1].type == MT_NOTE_ON && l.m[1].d1 == 0x3C && l.m[1].d2 == 0x64);
    CHECK(l.m[1].t_us == 0);           /* timestamp = first byte */
    CHECK(l.m[2].type == MT_NOTE_ON && l.m[2].d1 == 0x3E && l.m[2].d2 == 0x00);
    CHECK(l.m[3].type == MT_ACTIVE_SENSE);
    CHECK(l.m[4].type == MT_NOTE_ON && l.m[4].d1 == 0x40 && l.m[4].d2 == 0x7F);
    CHECK(stray == 0);

    /* sysex with interleaved realtime, then system common clears running status */
    l.n = 0;
    const uint8_t s2[] = { 0xB2, 0x07, 0x64, 0xF0, 0x41, 0x10, 0xF8, 0x42, 0xF7,
                           0x08, 0x09,              /* running status still B2 after sysex? no: cleared */
                           0xB2, 0x07, 0x10, 0xF1, 0x23, 0x01, 0x02 };
    parse_bytes(s2, sizeof s2, &l, &stray);
    /* expected: CC, CLOCK, SYSEX(6), [08 09 stray], CC, MTC QF, [01 02 stray] */
    CHECK(l.n == 5);
    CHECK(l.m[0].type == MT_CC && l.m[0].d1 == 7 && l.m[0].d2 == 100);
    CHECK(l.m[1].type == MT_CLOCK);
    CHECK(l.m[2].type == MT_SYSEX && l.m[2].sx_len == 5 && l.m[2].flags == 0);
    CHECK(l.m[2].sx[4] == 0xF7);
    CHECK(l.m[3].type == MT_CC && l.m[3].d2 == 0x10);
    CHECK(l.m[4].type == MT_MTC_QF && l.m[4].d1 == 0x23);
    CHECK(stray == 4);

    /* long sysex: truncated store, full length */
    l.n = 0;
    uint8_t s3[300];
    s3[0] = 0xF0;
    for (int i = 1; i < 299; i++) s3[i] = (uint8_t)(i & 0x7F);
    s3[299] = 0xF7;
    parse_bytes(s3, 300, &l, &stray);
    CHECK(l.n == 1 && l.m[0].sx_len == 300 && (l.m[0].flags & MSG_F_SX_TRUNC));
    decode(&l.m[0], t);
    CHECK_STR(t, "SYSEX 300b id01");

    /* unterminated sysex ended by a status byte */
    l.n = 0;
    const uint8_t s4[] = { 0xF0, 0x43, 0x10, 0xC0, 0x05 };
    parse_bytes(s4, sizeof s4, &l, &stray);
    CHECK(l.n == 2 && l.m[0].type == MT_SYSEX && (l.m[0].flags & MSG_F_SX_UNTERM));
    CHECK(l.m[1].type == MT_PROGRAM && l.m[1].d1 == 5);
    decode(&l.m[0], t);
    CHECK_STR(t, "SYSEX 3b id43!");

    /* new status abandons a partial message */
    l.n = 0;
    const uint8_t s5[] = { 0x90, 0x3C, 0x80, 0x3C, 0x00 };
    parse_bytes(s5, sizeof s5, &l, &stray);
    CHECK(l.n == 1 && l.m[0].type == MT_NOTE_OFF);

    /* two byte messages with running status */
    l.n = 0;
    const uint8_t s6[] = { 0xC3, 0x01, 0x02, 0x03, 0xF6, 0xF2, 0x10, 0x20 };
    parse_bytes(s6, sizeof s6, &l, &stray);
    CHECK(l.n == 5);
    CHECK(l.m[2].type == MT_PROGRAM && l.m[2].d1 == 3);
    CHECK(l.m[3].type == MT_TUNE_REQ);
    CHECK(l.m[4].type == MT_SONG_POS && l.m[4].d1 == 0x10 && l.m[4].d2 == 0x20);

    ml_free(&l);
}

/* ------------------------------------------------------------------------ */
/* Decoder tests                                                             */
/* ------------------------------------------------------------------------ */
static void dec_bytes(const uint8_t *b, int n, char *t, bool c3) {
    msglist_t l = ml_new();
    parse_bytes(b, n, &l, NULL);
    decode_opts_t o = { c3, 0 };
    if (l.n) midi_decode(&l.m[l.n - 1], &o, t, MIDI_TEXT_MAX);
    else strcpy(t, "<none>");
    ml_free(&l);
}

static void test_decode(void) {
    printf("decode\n");
    char t[MIDI_TEXT_MAX];
#define D(str, c3, ...) do { const uint8_t b_[] = { __VA_ARGS__ }; \
        dec_bytes(b_, sizeof b_, t, c3); CHECK_STR(t, str); CHECK(strlen(t) <= 21); } while (0)
    D("NOTE ON  1 C4   100", false, 0x90, 60, 100);
    D("NOTE ON  1 C3   100", true,  0x90, 60, 100);
    D("NOTE OFF 1 C4     0", false, 0x80, 60, 0);
    D("NOTE ON 16 C#-1 127", false, 0x9F, 1, 127);
    D("NOTE ON 16 G9   127", false, 0x9F, 127, 127);
    D("CC  3 #7=127",        false, 0xB2, 7, 127);
    D("PC  1 12",            false, 0xC0, 12);
    D("PB  1 +8191",         false, 0xE0, 0x7F, 0x7F);
    D("PB  1 -8192",         false, 0xE0, 0x00, 0x00);
    D("PB  1 +0",            false, 0xE0, 0x00, 0x40);
    D("AT  1 64",            false, 0xD0, 64);
    D("PAT  1 C4    64",      false, 0xA0, 60, 64);
    D("SYSEX 12b id00201F",  false, 0xF0, 0x00, 0x20, 0x1F, 1, 2, 3, 4, 5, 6, 7, 0xF7);
    D("MSC ALL GO",          false, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x01, 0xF7);
    D("MSC LTG GO Q12.5 @1", false, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0xF7);
    D("MSC ALL STOP",        false, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x02, 0xF7);
    D("MSC ALL RESUME",      false, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x03, 0xF7);
    D("MMC PLAY",            false, 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7);
    D("MMC STOP",            false, 0xF0, 0x7F, 0x7F, 0x06, 0x01, 0xF7);
    D("MTC 01:02:03:04",     false, 0xF0, 0x7F, 0x7F, 0x01, 0x01, 0x61, 2, 3, 4, 0xF7);
    D("MTC QF 3:5",          false, 0xF1, 0x35);
    D("SONG POS 1234",       false, 0xF2, 1234 & 0x7F, 1234 >> 7);
    D("CLOCK",               false, 0xF8);
    D("START",               false, 0xFA);
    D("CONTINUE",            false, 0xFB);
    D("STOP",                false, 0xFC);
    D("UNDEF F4",            false, 0xF4);
#undef D
    midi_format_time(83456789ull, t, sizeof t);
    CHECK_STR(t, "01:23.456");
}

/* ------------------------------------------------------------------------ */
/* Engine helpers                                                            */
/* ------------------------------------------------------------------------ */
static me_t g_me;
static me_settings_t g_set;

static void eng_reset(void) {
    g_set.thru[0] = g_set.thru[1] = true;
    g_set.gen[0] = g_set.gen[1] = true;
    me_init(&g_me, &g_set);
}

/* feed bytes, servicing after each, collecting everything out on port A */
static int drain(int port, uint8_t *out, int max) {
    int n = 0;
    uint8_t b;
    while (n < max && me_out_pop(&g_me, port, &b)) out[n++] = b;
    return n;
}

static int feed(const uint8_t *in, int n, uint64_t *t) {
    for (int i = 0; i < n; i++) {
        me_input_byte(&g_me, in[i], *t);
        me_service(&g_me, *t);
        *t += 320;
    }
    return n;
}

/* feed bytes and let both UARTs keep up (drain after every byte) */
static int feed_drain(const uint8_t *in, int n, uint64_t *t, uint8_t *oa, uint8_t *ob) {
    int na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        me_input_byte(&g_me, in[i], *t);
        me_service(&g_me, *t);
        na += drain(0, oa + na, 256);
        nb += drain(1, ob + nb, 256);
        *t += 320;
    }
    return na | (nb << 16);
}

static bool bytes_eq(const uint8_t *a, int na, const uint8_t *b, int nb) {
    if (na != nb) { printf("  len %d != %d\n", na, nb); }
    if (na != nb || memcmp(a, b, (size_t)na) != 0) {
        printf("  got: "); for (int i = 0; i < na; i++) printf("%02X ", a[i]);
        printf("\n  exp: "); for (int i = 0; i < nb; i++) printf("%02X ", b[i]);
        printf("\n");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Directed engine tests                                                     */
/* ------------------------------------------------------------------------ */
static void test_engine_directed(void) {
    printf("engine (directed)\n");
    uint8_t out[512];
    uint64_t t = 1000;
    int n;

    /* 1. plain thru is byte-identical on both outputs */
    eng_reset();
    {
        const uint8_t in[] = { 0x90, 0x3C, 0x64, 0x3E, 0x64, 0xF8, 0xF0, 0x01, 0xF8, 0x02, 0xF7,
                               0xB0, 0x07, 0x7F, 0x08, 0x10, 0xF1, 0x20, 0xF6, 0xF7 };
        uint8_t ob[512];
        int r = feed_drain(in, sizeof in, &t, out, ob);
        CHECK(bytes_eq(out, r & 0xFFFF, in, sizeof in));
        CHECK(bytes_eq(ob, r >> 16, in, sizeof in));
    }

    /* 2. generated message merges at boundary; running status re-sent */
    eng_reset();
    {
        const uint8_t a[] = { 0x90, 0x3C, 0x64 };
        const uint8_t g[] = { 0xB0, 0x07, 0x7F };
        const uint8_t b[] = { 0x3E, 0x64, 0x40, 0x00 };
        const uint8_t exp[] = { 0x90, 0x3C, 0x64, 0xB0, 0x07, 0x7F, 0x90, 0x3E, 0x64, 0x40, 0x00 };
        feed(a, 3, &t);
        CHECK(me_gen_push(&g_me, g, 3));
        me_service(&g_me, t);
        feed(b, 4, &t);
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, exp, sizeof exp));
    }

    /* 3. generated message waits while the input is mid-message */
    eng_reset();
    {
        const uint8_t a[] = { 0x90, 0x3C };
        const uint8_t g[] = { 0xC5, 0x09 };
        const uint8_t b[] = { 0x64, 0x3E, 0x50 };
        const uint8_t exp[] = { 0x90, 0x3C, 0x64, 0xC5, 0x09, 0x90, 0x3E, 0x50 };
        feed(a, 2, &t);
        CHECK(me_gen_push(&g_me, g, 2));
        me_service(&g_me, t);
        CHECK(me_out_pending(&g_me, 0) == 2);     /* not inserted yet */
        feed(b, 1, &t);                            /* completes: boundary */
        feed(b + 1, 2, &t);
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, exp, sizeof exp));
    }

    /* 4. sysex passes intact; generated message waits; generated realtime goes straight in */
    eng_reset();
    {
        const uint8_t a[] = { 0xF0, 0x7F, 0x7F, 0x02 };
        const uint8_t g[] = { 0x91, 0x40, 0x40 };
        const uint8_t b[] = { 0x7F, 0x01, 0xF7 };
        const uint8_t exp[] = { 0xFA, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x01, 0xF7, 0x91, 0x40, 0x40 };
        feed(a, 4, &t);
        CHECK(me_gen_push(&g_me, g, 3));
        CHECK(me_gen_rt_push(&g_me, 0xFA));
        me_service(&g_me, t);
        /* drain so far: realtime must be available right away */
        n = drain(0, out, sizeof out);
        feed(b, 3, &t);
        n += drain(0, out + n, (int)sizeof out - n);
        /* realtime jumps the queue: it came out before any further byte */
        CHECK(bytes_eq(out, n, exp, sizeof exp));
    }

    /* 5. stall timeout forces boundary; stalled remainder dropped; running status restored */
    eng_reset();
    {
        const uint8_t a[] = { 0x90, 0x3C };
        const uint8_t g[] = { 0xB0, 0x40, 0x00 };
        const uint8_t b[] = { 0x64, 0x3E, 0x50 };   /* 64 = late remainder (dropped) */
        const uint8_t expA[] = { 0x90, 0x3C, 0xB0, 0x40, 0x00, 0x90, 0x3E, 0x50 };
        feed(a, 2, &t);
        CHECK(me_gen_push(&g_me, g, 3));
        me_service(&g_me, t + 50000);
        CHECK(me_out_pending(&g_me, 0) == 2);
        t += ME_STALL_MSG_US + 1000;
        me_service(&g_me, t);
        CHECK(g_me.stats.stalls == 1);
        feed(b, 3, &t);
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, expA, sizeof expA));
    }

    /* 6. thru off takes effect at the next message, never mid-message */
    eng_reset();
    {
        const uint8_t a[] = { 0x90, 0x3C };
        const uint8_t b[] = { 0x64, 0x3E, 0x50, 0xF8 };
        const uint8_t expA[] = { 0x90, 0x3C, 0x64 };
        uint8_t ob[64];
        int r = feed_drain(a, 2, &t, out, ob);
        g_set.thru[0] = false;
        int r2 = feed_drain(b, 4, &t, out + (r & 0xFFFF), ob + (r >> 16));
        CHECK(bytes_eq(out, (r & 0xFFFF) + (r2 & 0xFFFF), expA, sizeof expA));
        /* B unaffected */
        const uint8_t expB[] = { 0x90, 0x3C, 0x64, 0x3E, 0x50, 0xF8 };
        CHECK(bytes_eq(ob, (r >> 16) + (r2 >> 16), expB, sizeof expB));
        /* re-enable mid running-status stream: joins at next message with status */
        const uint8_t c[] = { 0x40 };
        feed(c, 1, &t);              /* first byte of next msg while A still off */
        g_set.thru[0] = true;
        const uint8_t d[] = { 0x10, 0x41, 0x11 };
        feed(d, 3, &t);
        const uint8_t expA2[] = { 0x41, 0x11 };  /* last status on A is still 90 */
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, expA2, sizeof expA2));
    }

    /* 7. generator routing: gen only to B */
    eng_reset();
    {
        g_set.gen[0] = false;
        const uint8_t g[] = { 0xC0, 0x01 };
        CHECK(me_gen_push(&g_me, g, 2));
        me_service(&g_me, t);
        CHECK(me_out_pending(&g_me, 0) == 0);
        CHECK(me_out_pending(&g_me, 1) == 2);
    }

    /* 8. orphan data bytes after system common are dropped and counted */
    eng_reset();
    {
        const uint8_t in[] = { 0x90, 0x3C, 0x64, 0xF6, 0x3E, 0x64 };
        const uint8_t exp[] = { 0x90, 0x3C, 0x64, 0xF6 };
        feed(in, sizeof in, &t);
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, exp, sizeof exp));
        CHECK(g_me.stats.orphan_data == 2);
    }

    /* 9. stalled sysex: generated message waits for the long timeout, then the
     *    rest of the sysex is dropped on that output (receiver already aborted it) */
    eng_reset();
    {
        const uint8_t a[] = { 0xF0, 0x41, 0x10 };
        const uint8_t g[] = { 0x92, 0x30, 0x30 };
        const uint8_t b[] = { 0x11, 0x12, 0xF7, 0xC0, 0x02 };
        const uint8_t exp[] = { 0xF0, 0x41, 0x10, 0x92, 0x30, 0x30, 0xC0, 0x02 };
        feed(a, 3, &t);
        CHECK(me_gen_push(&g_me, g, 3));
        me_service(&g_me, t + ME_STALL_MSG_US * 2);   /* short timeout does not apply */
        CHECK(me_out_pending(&g_me, 0) == 3);
        t += ME_STALL_SYSEX_US + 1000;
        me_service(&g_me, t);
        feed(b, 5, &t);
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, exp, sizeof exp));
    }
}

/* ------------------------------------------------------------------------ */
/* Randomised merge property test                                            */
/* ------------------------------------------------------------------------ */
static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define STREAM_MAX 400000
static uint8_t s_in[STREAM_MAX];
static uint8_t s_out[ME_NUM_OUTS][STREAM_MAX * 2];

/* Build a random input stream: channels 1-15 only (16 is reserved for the
 * generator), running status, sysex (id 0x41), system common, clock (F8)
 * sprinkled anywhere. */
static int build_stream(int nmsgs) {
    int n = 0;
    uint8_t running = 0;
    for (int i = 0; i < nmsgs && n < STREAM_MAX - 300; i++) {
        uint32_t r = rnd() % 100;
#define PUT(x) do { s_in[n++] = (uint8_t)(x); if (rnd() % 23 == 0) s_in[n++] = 0xF8; } while (0)
        if (r < 70) {
            static const uint8_t types[] = { 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 };
            uint8_t st = (uint8_t)(types[rnd() % 7] | (rnd() % 15));
            if (rnd() % 3 && running) st = running;      /* favour running status */
            if (st != running || rnd() % 5 == 0) PUT(st);
            running = st;
            for (int k = 0; k < midi_data_len(st); k++) PUT(rnd() & 0x7F);
        } else if (r < 85) {
            int len = (int)(rnd() % 120);
            PUT(0xF0); PUT(0x41);
            for (int k = 0; k < len; k++) PUT(rnd() & 0x7F);
            PUT(0xF7);
            running = 0;
        } else if (r < 92) {
            static const uint8_t sc[] = { 0xF1, 0xF2, 0xF3, 0xF6 };
            uint8_t st = sc[rnd() % 4];
            PUT(st);
            for (int k = 0; k < midi_data_len(st); k++) PUT(rnd() & 0x7F);
            running = 0;
        } else {
            PUT(0xF8);
        }
#undef PUT
    }
    return n;
}

static bool is_gen_msg(const midi_msg_t *m) {
    if (m->type == MT_SYSEX) return m->sx_len > 1 && m->sx[1] == 0x7D;
    if (m->status >= 0x80 && m->status < 0xF0) return (m->status & 0x0F) == 0x0F;
    return false;
}

static int make_gen(uint8_t *g) {
    uint32_t r = rnd() % 10;
    if (r < 7) {
        static const uint8_t types[] = { 0x80, 0x90, 0xB0, 0xC0, 0xE0 };
        uint8_t st = (uint8_t)(types[rnd() % 5] | 0x0F);
        g[0] = st;
        int n = 1 + midi_data_len(st);
        for (int k = 1; k < n; k++) g[k] = rnd() & 0x7F;
        return n;
    }
    int len = 4 + (int)(rnd() % 40);
    g[0] = 0xF0; g[1] = 0x7D;
    for (int k = 2; k < len - 1; k++) g[k] = rnd() & 0x7F;
    g[len - 1] = 0xF7;
    return len;
}

static void run_merge(const char *name, int nmsgs, int idle_pct, int gen_per_mille, bool dense_gen) {
    printf("engine merge: %s\n", name);
    eng_reset();
    int n = build_stream(nmsgs);

    static uint8_t gen_log[200000];
    int gen_log_n = 0;
    msglist_t gen_expect = ml_new();
    uint32_t gen_rt_ok = 0;
    int outn[ME_NUM_OUTS] = { 0, 0 };
    uint64_t t = 0;
    uint32_t maxq = 0;
    int i = 0;

    /* one tick = one MIDI byte time (320 us). Each tick: maybe an input byte,
     * maybe a generator request, service, and each UART sends one byte. */
    while (i < n || me_out_pending(&g_me, 0) || me_out_pending(&g_me, 1) ||
           spsc_count(&g_me.genq_idx)) {
        if (i < n && (int)(rnd() % 100) >= idle_pct) {
            me_input_byte(&g_me, s_in[i++], t);
        }
        if (i < n && (int)(rnd() % 1000) < gen_per_mille) {
            uint8_t g[ME_GEN_MAX_LEN];
            int gl = make_gen(g);
            if (me_gen_push(&g_me, g, (uint8_t)gl)) {
                midi_parser_t p;
                midi_parser_init(&p, collect, &gen_expect);
                for (int k = 0; k < gl; k++) midi_parser_feed(&p, g[k], 0);
                if (gen_log_n + gl < (int)sizeof gen_log) { memcpy(gen_log + gen_log_n, g, (size_t)gl); gen_log_n += gl; }
            }
            if (dense_gen && me_gen_rt_push(&g_me, 0xFE)) gen_rt_ok++;
        }
        if (i < n && rnd() % 200 == 0 && me_gen_rt_push(&g_me, 0xFE)) gen_rt_ok++;
        me_service(&g_me, t);
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            uint8_t b;
            if (me_out_pending(&g_me, p) > maxq) maxq = me_out_pending(&g_me, p);
            if (me_out_pop(&g_me, p, &b)) s_out[p][outn[p]++] = b;
        }
        t += 320;
        if (t > 3600ull * 1000000ull) break;
    }

    /* expected input messages */
    msglist_t in_msgs = ml_new();
    uint32_t in_stray;
    parse_bytes(s_in, n, &in_msgs, &in_stray);
    int in_clock = 0;
    for (int k = 0; k < n; k++) if (s_in[k] == 0xF8) in_clock++;

    for (int p = 0; p < ME_NUM_OUTS; p++) {
        msglist_t om = ml_new();
        uint32_t stray;
        parse_bytes(s_out[p], outn[p], &om, &stray);
        int ii = 0, gi = 0, clk = 0, fe = 0, bad = 0;
        for (int k = 0; k < om.n; k++) {
            const midi_msg_t *m = &om.m[k];
            if (m->type == MT_CLOCK) { clk++; continue; }
            if (m->type == MT_ACTIVE_SENSE) { fe++; continue; }
            if (is_gen_msg(m)) {
                if (gi >= gen_expect.n || !msg_eq(m, &gen_expect.m[gi])) bad++;
                gi++;
            } else {
                while (ii < in_msgs.n && in_msgs.m[ii].type == MT_CLOCK) ii++;
                if (ii >= in_msgs.n || !msg_eq(m, &in_msgs.m[ii])) {
                    if (bad < 3) {
                        char a[32], b[32] = "-";
                        decode(m, a);
                        if (ii < in_msgs.n) decode(&in_msgs.m[ii], b);
                        printf("  port %d mismatch at in-msg %d: got \"%s\" exp \"%s\"\n", p, ii, a, b);
                    }
                    bad++;
                }
                ii++;
            }
        }
        while (ii < in_msgs.n && in_msgs.m[ii].type == MT_CLOCK) ii++;
        printf("  port %c: %d bytes out, %d input msgs ok, %d gen msgs, %d clocks, %d gen RT, drops %u\n",
               'A' + p, outn[p], ii, gi, clk, fe, (unsigned)g_me.out[p].drops);
        CHECK(bad == 0);
        CHECK(ii == in_msgs.n);
        CHECK(gi == gen_expect.n);
        CHECK(clk == in_clock);
        CHECK(fe == (int)(gen_rt_ok - g_me.out[p].gen_shed));
        CHECK(stray == 0);
        CHECK(g_me.out[p].drops == 0);
        ml_free(&om);
    }
    CHECK(in_stray == 0);
    CHECK(g_me.stats.orphan_data == 0);
    CHECK(g_me.stats.stalls == 0);
    printf("  input: %d bytes, %d msgs; generator accepted %d msgs, %u RT; max TX backlog %u bytes (%.1f ms)\n",
           n, in_msgs.n, gen_expect.n, (unsigned)gen_rt_ok, (unsigned)maxq, maxq * 0.32);
    /* thru latency added by merging stays bounded */
    CHECK(maxq <= ME_GEN_BACKLOG_MAX + ME_GEN_MAX_LEN + 8);
    ml_free(&in_msgs);
    ml_free(&gen_expect);
}

int main(void) {
    test_parser();
    test_decode();
    test_msc();
    test_engine_directed();
    run_merge("sparse input, moderate generator", 20000, 50, 20, false);
    run_merge("dense input (100%), heavy generator", 40000, 0, 50, true);
    run_merge("70% input, very heavy generator", 40000, 30, 300, true);
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
