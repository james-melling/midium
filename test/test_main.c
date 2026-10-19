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
#include "settings.h"

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

/* Default routing (as 0.2): IN A to both outputs, generator to both, IN B
 * monitored only. */
static void eng_reset(void) {
    memset(&g_set, 0, sizeof g_set);
    g_set.src[0][ME_SRC_A] = g_set.src[1][ME_SRC_A] = true;
    g_set.src[0][ME_SRC_GEN] = g_set.src[1][ME_SRC_GEN] = true;
    me_init(&g_me, &g_set);
}

/* Routing from a compact spec: route[out] = bitmask 1 = IN A, 2 = IN B, 4 = gen. */
static void eng_reset_route(int ra, int rb) {
    eng_reset();
    for (int s = 0; s < ME_NUM_SRCS; s++) {
        g_set.src[0][s] = (ra >> s) & 1;
        g_set.src[1][s] = (rb >> s) & 1;
    }
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
        me_input_byte(&g_me, ME_IN_A, in[i], *t);
        me_service(&g_me, *t);
        *t += 320;
    }
    return n;
}

/* feed bytes and let both UARTs keep up (drain after every byte) */
static int feed_drain(const uint8_t *in, int n, uint64_t *t, uint8_t *oa, uint8_t *ob) {
    int na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        me_input_byte(&g_me, ME_IN_A, in[i], *t);
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
        g_set.src[0][ME_SRC_A] = false;
        int r2 = feed_drain(b, 4, &t, out + (r & 0xFFFF), ob + (r >> 16));
        CHECK(bytes_eq(out, (r & 0xFFFF) + (r2 & 0xFFFF), expA, sizeof expA));
        /* B unaffected */
        const uint8_t expB[] = { 0x90, 0x3C, 0x64, 0x3E, 0x50, 0xF8 };
        CHECK(bytes_eq(ob, (r >> 16) + (r2 >> 16), expB, sizeof expB));
        /* re-enable mid running-status stream: joins at next message with status */
        const uint8_t c[] = { 0x40 };
        feed(c, 1, &t);              /* first byte of next msg while A still off */
        g_set.src[0][ME_SRC_A] = true;
        const uint8_t d[] = { 0x10, 0x41, 0x11 };
        feed(d, 3, &t);
        const uint8_t expA2[] = { 0x41, 0x11 };  /* last status on A is still 90 */
        n = drain(0, out, sizeof out);
        CHECK(bytes_eq(out, n, expA2, sizeof expA2));
    }

    /* 7. generator routing: gen only to B */
    eng_reset();
    {
        g_set.src[0][ME_SRC_GEN] = false;
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
        CHECK(g_me.in[ME_IN_A].st.orphan_data == 2);
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
    while (i < n || !me_idle(&g_me)) {
        if (i < n && (int)(rnd() % 100) >= idle_pct) {
            me_input_byte(&g_me, ME_IN_A, s_in[i++], t);
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
    CHECK(g_me.in[ME_IN_A].st.orphan_data == 0);
    CHECK(g_me.stats.stalls == 0);
    printf("  input: %d bytes, %d msgs; generator accepted %d msgs, %u RT; max TX backlog %u bytes (%.1f ms)\n",
           n, in_msgs.n, gen_expect.n, (unsigned)gen_rt_ok, (unsigned)maxq, maxq * 0.32);
    /* thru latency added by merging stays bounded */
    CHECK(maxq <= ME_GEN_BACKLOG_MAX + ME_GEN_MAX_LEN + 8);
    ml_free(&in_msgs);
    ml_free(&gen_expect);
}

/* ------------------------------------------------------------------------ */
/* Two inputs: directed tests                                                */
/* ------------------------------------------------------------------------ */
static void in_bytes(int in, const uint8_t *b, int n, uint64_t *t) {
    for (int i = 0; i < n; i++) {
        me_input_byte(&g_me, in, b[i], *t);
        me_service(&g_me, *t);
        *t += 320;
    }
}
#define IN(in, ...) do { const uint8_t b_[] = { __VA_ARGS__ }; in_bytes(in, b_, (int)sizeof b_, &t); } while (0)
#define EXPECT(port, ...) do { const uint8_t e_[] = { __VA_ARGS__ }; uint8_t o_[4096]; \
        int n_ = drain(port, o_, (int)sizeof o_); CHECK(bytes_eq(o_, n_, e_, (int)sizeof e_)); } while (0)
#define EXPECT_NONE(port) do { uint8_t o_[64]; CHECK(drain(port, o_, 64) == 0); } while (0)

static void test_engine_two_inputs(void) {
    printf("engine (two inputs, directed)\n");
    uint64_t t = 1000;

    /* 1. merge: IN B waits for IN A's message to end, then goes in whole */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0x90, 0x3C);
    IN(ME_IN_B, 0xB1, 0x07, 0x7F);
    CHECK(me_out_pending(&g_me, 0) == 2 && me_out_pending(&g_me, 1) == 2);
    IN(ME_IN_A, 0x64);
    EXPECT(0, 0x90, 0x3C, 0x64, 0xB1, 0x07, 0x7F);
    EXPECT(1, 0x90, 0x3C, 0x64, 0xB1, 0x07, 0x7F);
    CHECK(me_idle(&g_me));

    /* 2. running status per output: re-sent when the other input changed it */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0x90, 0x3C, 0x64);
    IN(ME_IN_B, 0x91, 0x40, 0x40);
    IN(ME_IN_A, 0x3E, 0x64);
    IN(ME_IN_B, 0x41, 0x41);
    IN(ME_IN_A, 0x3F, 0x10);
    IN(ME_IN_A, 0x40, 0x11);         /* same source again: no re-send */
    EXPECT(0, 0x90, 0x3C, 0x64, 0x91, 0x40, 0x40, 0x90, 0x3E, 0x64, 0x91, 0x41, 0x41,
              0x90, 0x3F, 0x10, 0x40, 0x11);

    /* 3. sysex is never interleaved; realtime from the other input is */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0xF0, 0x41, 0x01, 0x02);
    EXPECT(0, 0xF0, 0x41, 0x01, 0x02);
    IN(ME_IN_B, 0xC1, 0x05, 0xFD, 0x92, 0x10);
    EXPECT(0, 0xFD);                 /* realtime straight through, mid-sysex */
    IN(ME_IN_B, 0x10);
    EXPECT_NONE(0);
    IN(ME_IN_A, 0x03, 0xF7);
    EXPECT(0, 0x03, 0xF7, 0xC1, 0x05, 0x92, 0x10, 0x10);

    /* 4. B's sysex waits for A's message, and A's next message waits for it */
    eng_reset_route(3, 0);
    IN(ME_IN_A, 0xB0, 0x07);
    IN(ME_IN_B, 0xF0, 0x42, 0x11);
    IN(ME_IN_A, 0x7F);               /* A done: B's buffered sysex start goes out */
    IN(ME_IN_A, 0xC0, 0x01);         /* waits: B is mid-sysex on OUT A */
    IN(ME_IN_B, 0x12, 0xF7);
    EXPECT(0, 0xB0, 0x07, 0x7F, 0xF0, 0x42, 0x11, 0x12, 0xF7, 0xC0, 0x01);
    EXPECT_NONE(1);

    /* 5. routing changes latch at each input's next message start */
    eng_reset_route(1, 2);                           /* split */
    IN(ME_IN_A, 0x90, 0x3C);
    route_preset_apply(&g_set, 1);                   /* merge -> both */
    IN(ME_IN_A, 0x64);
    EXPECT_NONE(1);                                  /* no half message on OUT B */
    IN(ME_IN_A, 0x3E, 0x50);
    EXPECT(1, 0x90, 0x3E, 0x50);                     /* status re-sent: B had none */
    EXPECT(0, 0x90, 0x3C, 0x64, 0x3E, 0x50);
    IN(ME_IN_A, 0xB0, 0x07);
    route_preset_apply(&g_set, 3);                   /* B -> both: A removed mid-message */
    IN(ME_IN_A, 0x7F);                               /* finishes where it started */
    IN(ME_IN_A, 0x08, 0x10);                         /* next message: routed nowhere */
    IN(ME_IN_B, 0x93, 0x20, 0x21);
    EXPECT(0, 0xB0, 0x07, 0x7F, 0x93, 0x20, 0x21);
    EXPECT(1, 0xB0, 0x07, 0x7F, 0x93, 0x20, 0x21);

    /* 6. stall with two inputs: A stops mid-message, B waits 100 ms, then
     *    the boundary is forced; A's late remainder is dropped there */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0x90, 0x3C);
    uint64_t t_a = t - 320;
    IN(ME_IN_B, 0xC1, 0x05);
    me_service(&g_me, t_a + ME_STALL_MSG_US - 1000);
    CHECK(me_out_pending(&g_me, 0) == 2);
    t = t_a + ME_STALL_MSG_US + 1000;
    me_service(&g_me, t);
    CHECK(g_me.stats.stalls == 1 && g_me.in[ME_IN_A].st.stalls == 1);
    IN(ME_IN_A, 0x64);                               /* late: dropped */
    IN(ME_IN_A, 0x3E, 0x50);
    EXPECT(0, 0x90, 0x3C, 0xC1, 0x05, 0x90, 0x3E, 0x50);
    EXPECT(1, 0x90, 0x3C, 0xC1, 0x05, 0x90, 0x3E, 0x50);
    CHECK(g_me.stats.stalls == 1);                   /* counted once per message */

    /* 6b. a slow input with nobody waiting is never cut */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0x90, 0x3C);
    t += 2 * ME_STALL_SYSEX_US;
    me_service(&g_me, t);
    IN(ME_IN_A, 0x64);
    EXPECT(0, 0x90, 0x3C, 0x64);
    CHECK(g_me.stats.stalls == 0);

    /* 6c. B stalls mid-message (it owns OUT A), A waits, then goes */
    eng_reset_route(3, 3);
    IN(ME_IN_B, 0xE1, 0x00);
    uint64_t t_b = t - 320;
    EXPECT(0, 0xE1, 0x00);
    IN(ME_IN_A, 0xF8, 0xC0, 0x09);                   /* clock passes at once */
    EXPECT(0, 0xF8);
    t = t_b + ME_STALL_MSG_US + 1000;
    me_service(&g_me, t);
    EXPECT(0, 0xC0, 0x09);
    CHECK(g_me.in[ME_IN_B].st.stalls == 1 && g_me.in[ME_IN_A].st.stalls == 0);

    /* 7. stalled sysex: the long timeout applies */
    eng_reset_route(3, 3);
    IN(ME_IN_A, 0xF0, 0x41, 0x01);
    t_a = t - 320;
    IN(ME_IN_B, 0xC1, 0x05);
    me_service(&g_me, t_a + ME_STALL_MSG_US * 3);
    CHECK(me_out_pending(&g_me, 0) == 3);
    t = t_a + ME_STALL_SYSEX_US + 1000;
    me_service(&g_me, t);
    IN(ME_IN_A, 0x02, 0xF7, 0xC0, 0x02);
    EXPECT(0, 0xF0, 0x41, 0x01, 0xC1, 0x05, 0xC0, 0x02);

    /* 8. realtime goes only where its input is routed */
    eng_reset_route(1, 2);
    IN(ME_IN_B, 0xFD);
    IN(ME_IN_A, 0xF8);
    EXPECT(0, 0xF8);
    EXPECT(1, 0xFD);

    /* 9. generator + two inputs: generator waits for whichever input owns
     *    the output, per output */
    eng_reset_route(1 | 4, 2 | 4);
    IN(ME_IN_A, 0x90, 0x3C);                         /* OUT A busy, OUT B free */
    CHECK(me_gen_push(&g_me, (const uint8_t[]){ 0xCF, 0x07 }, 2));
    me_service(&g_me, t);
    EXPECT(1, 0xCF, 0x07);                           /* OUT B did not wait for IN A */
    IN(ME_IN_A, 0x64);
    EXPECT(0, 0x90, 0x3C, 0x64, 0xCF, 0x07);

    /* 10. a B message bigger than its lane, while A owns the output: dropped
     *     whole and counted; A intact; B's next message passes */
    eng_reset_route(3, 2);
    {
        static uint8_t ob[ME_OUTQ_SIZE * 4];
        int na = 0, nb = 0;
        uint8_t b;
        me_input_byte(&g_me, ME_IN_A, 0xF0, t);
        for (int k = 0; k < 3000; k++) {
            me_input_byte(&g_me, ME_IN_A, 0x41, t);
            if (k == 10) me_input_byte(&g_me, ME_IN_B, 0xF0, t);
            else if (k > 10 && k < 1600) me_input_byte(&g_me, ME_IN_B, (uint8_t)(k & 0x7F), t);
            else if (k == 1600) me_input_byte(&g_me, ME_IN_B, 0xF7, t);
            else if (k == 1601) me_input_byte(&g_me, ME_IN_B, 0xC2, t);
            else if (k == 1602) me_input_byte(&g_me, ME_IN_B, 0x03, t);
            me_service(&g_me, t);
            if (me_out_pop(&g_me, 0, &b)) na++;
            if (me_out_pop(&g_me, 1, &b)) ob[nb++] = b;
            t += 320;
        }
        me_input_byte(&g_me, ME_IN_A, 0xF7, t);
        uint8_t oa[8192];
        na += drain(0, oa, (int)sizeof oa);
        CHECK(na == 3002 + 2);                       /* A's sysex + B's C2 03 */
        CHECK(g_me.out[0].merge_drops[ME_IN_B] == 1);
        CHECK(g_me.out[1].merge_drops[ME_IN_B] == 0);
        nb += drain(1, ob + nb, (int)sizeof ob - nb);
        CHECK(nb == 1591 + 2);                       /* OUT B (B only): all of it */
        CHECK(g_me.out[0].drops == 0 && g_me.out[1].drops == 0);
    }

    /* 11. a big B sysex while the output is free passes whole, directly */
    eng_reset_route(3, 3);
    {
        uint8_t b;
        int n = 0, lat_bad = 0;
        for (int k = 0; k < 6000; k++) {
            uint8_t x = k == 0 ? 0xF0 : k == 5999 ? 0xF7 : (uint8_t)(k & 0x7F);
            me_input_byte(&g_me, ME_IN_B, x, t);
            me_service(&g_me, t);
            if (me_out_pop(&g_me, 0, &b)) { n++; if (b != x) lat_bad++; } else lat_bad++;
            t += 320;
        }
        CHECK(n == 6000 && lat_bad == 0);            /* each byte out in its own byte time */
        CHECK(g_me.out[0].merge_drops[ME_IN_B] == 0);
    }
}

/* Monitor ring: entries are tagged with their input; a gap is per input. */
static void test_engine_monitor(void) {
    printf("engine monitor ring\n");
    eng_reset();
    me_mon_entry_t e;
    for (unsigned k = 0; k < ME_MONQ_SIZE; k++) me_mon_push(&g_me, ME_IN_A, 0x90, 0, k);
    me_mon_push(&g_me, ME_IN_B, 0x91, 0, 9999);     /* ring full: lost */
    CHECK(g_me.in[ME_IN_B].st.mon_overflow == 1 && g_me.in[ME_IN_A].st.mon_overflow == 0);
    unsigned n = 0;
    while (me_mon_pop(&g_me, &e)) { n++; if (e.src != ME_IN_A || e.flags) break; }
    CHECK(n == ME_MONQ_SIZE);
    me_mon_push(&g_me, ME_IN_A, 0x80, 0, 1);
    me_mon_push(&g_me, ME_IN_B, 0x81, ME_MON_F_ERR, 2);
    CHECK(me_mon_pop(&g_me, &e) && e.src == ME_IN_A && e.flags == 0);
    CHECK(me_mon_pop(&g_me, &e) && e.src == ME_IN_B && e.flags == (ME_MON_F_GAP | ME_MON_F_ERR));
    CHECK(!me_mon_pop(&g_me, &e));
}

/* Routing presets, header summary, monitor line widths. */
static void test_routing_ui(void) {
    printf("routing presets / display widths\n");
    me_settings_t r;
    memset(&r, 0, sizeof r);
    char a[4], b[4];
    static const char *const sum[ROUTE_PRESET_COUNT][2] = {
        { "a", "a" }, { "ab", "ab" }, { "a", "b" }, { "b", "b" } };
    for (int p = 0; p < ROUTE_PRESET_COUNT; p++) {
        r.src[0][ME_SRC_GEN] = true; r.src[1][ME_SRC_GEN] = false;
        route_preset_apply(&r, p);
        for (int q = 0; q < ROUTE_PRESET_COUNT; q++) CHECK(route_preset_active(&r, q) == (p == q));
        CHECK(r.src[0][ME_SRC_GEN] && !r.src[1][ME_SRC_GEN]);   /* generator left alone */
        route_summary(&r, 0, a, sizeof a);
        route_summary(&r, 1, b, sizeof b);
        CHECK_STR(a, sum[p][0]);
        CHECK_STR(b, sum[p][1]);
        char hdr[32];
        snprintf(hdr, sizeof hdr, "%-7s A:%-2s B:%s", "PAUSED", a, b);
        CHECK(strlen(hdr) <= 19);                    /* room for the activity '*' */
        CHECK(strlen(route_preset_name(p)) <= 19);   /* + " *" marker on 21 cols */
    }
    memset(&r, 0, sizeof r);
    route_summary(&r, 0, a, sizeof a);
    CHECK_STR(a, "-");
    settings_defaults(&g_settings);
    CHECK(route_preset_active(&g_settings.route, 0));
    CHECK(g_settings.route.src[0][ME_SRC_GEN] && g_settings.route.src[1][ME_SRC_GEN]);

    /* Monitor lines lose 2 columns to the input tag: MSC adapts. */
    const uint8_t msc[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x04, 0x61, 0x02, 0x03, 0x04, 0x05,
                            '1', '2', '3', '.', '4', '5', 0x00, '6', '7', 0xF7 };
    msglist_t l = ml_new();
    parse_bytes(msc, sizeof msc, &l, NULL);
    CHECK(l.n == 1);
    for (unsigned w = 9; w <= 24; w++) {
        char t[MIDI_TEXT_MAX];
        decode_opts_t o = { false, (uint8_t)w };
        midi_decode(&l.m[0], &o, t, sizeof t);
        CHECK(strlen(t) <= w);
        if (w == 19) CHECK(strncmp(t, "MSC ", 4) == 0);
    }
    ml_free(&l);
}

/* ------------------------------------------------------------------------ */
/* Two inputs: randomised merge property test                                */
/* ------------------------------------------------------------------------ */
/* Input streams are tagged so every output message can be traced back:
 *   IN A: channels 1-7, sysex ID 41, F1/F3/F2 data < 40h, F6, clock F8.
 *   IN B: channels 8-15, sysex ID 42, F1/F3/F2 data >= 40h, realtime FD.
 *   generator: channel 16, sysex ID 7D, realtime FE. */
typedef struct {
    uint8_t ch_lo, ch_n, sx_id, sc_tag, rt;
    bool    tune_req;
    int     bigsx_pm;      /* per mille of messages that are big (2-10 kB) sysex */
    int     rt_div;        /* realtime after ~1 in rt_div bytes */
} streamcfg_t;

static const streamcfg_t SC_A = { 0, 7, 0x41, 0x00, 0xF8, true, 0, 23 };
static const streamcfg_t SC_B = { 7, 8, 0x42, 0x40, 0xFD, false, 0, 29 };

static int build_stream2(uint8_t *buf, uint8_t *start, int max, int nmsgs, const streamcfg_t *c) {
    int n = 0;
    uint8_t running = 0;
    for (int i = 0; i < nmsgs && n < max - 10100; i++) {
        uint32_t r = rnd() % 100;
        start[n] = 1;                 /* first byte of each message (or lone realtime) */
#define PUT(x) do { buf[n++] = (uint8_t)(x); start[n] = 0; \
                   if (rnd() % (uint32_t)c->rt_div == 0) { buf[n++] = c->rt; start[n] = 0; } } while (0)
        if (c->bigsx_pm && (int)(rnd() % 1000) < c->bigsx_pm) {
            int len = 2000 + (int)(rnd() % 8000);
            PUT(0xF0); PUT(c->sx_id);
            for (int k = 0; k < len; k++) PUT(rnd() & 0x7F);
            PUT(0xF7);
            running = 0;
        } else if (r < 70) {
            static const uint8_t types[] = { 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0 };
            uint8_t st = (uint8_t)(types[rnd() % 7] | (c->ch_lo + rnd() % c->ch_n));
            if (rnd() % 3 && running) st = running;
            if (st != running || rnd() % 5 == 0) PUT(st);
            running = st;
            for (int k = 0; k < midi_data_len(st); k++) PUT(rnd() & 0x7F);
        } else if (r < 85) {
            int len = (int)(rnd() % 120);
            PUT(0xF0); PUT(c->sx_id);
            for (int k = 0; k < len; k++) PUT(rnd() & 0x7F);
            PUT(0xF7);
            running = 0;
        } else if (r < 92) {
            static const uint8_t sc[] = { 0xF1, 0xF2, 0xF3, 0xF6 };
            uint8_t st = sc[rnd() % (c->tune_req ? 4 : 3)];
            PUT(st);
            if (st == 0xF1 || st == 0xF3) PUT(c->sc_tag | (rnd() & 0x3F));
            if (st == 0xF2) { PUT(rnd() & 0x7F); PUT(c->sc_tag | (rnd() & 0x3F)); }
            running = 0;
        } else {
            PUT(c->rt);
        }
#undef PUT
    }
    return n;
}

/* 0 = IN A, 1 = IN B, 2 = generator, -1 = realtime / unknown */
static int msg_source(const midi_msg_t *m) {
    if (m->type == MT_SYSEX) {
        uint8_t id = m->sx_len > 1 ? m->sx[1] : 0;
        return id == 0x41 ? 0 : id == 0x42 ? 1 : id == 0x7D ? 2 : -1;
    }
    if (m->status >= 0x80 && m->status < 0xF0) {
        unsigned ch = m->status & 0x0Fu;
        return ch == 15 ? 2 : ch >= 7 ? 1 : 0;
    }
    switch (m->status) {
    case 0xF1: case 0xF3: return m->d1 >= 0x40;
    case 0xF2: return m->d2 >= 0x40;
    case 0xF6: return 0;
    default: return -1;
    }
}

static void parse_ticks(const uint8_t *b, const uint32_t *tk, int n, msglist_t *out, uint32_t *stray) {
    midi_parser_t p;
    midi_parser_init(&p, collect, out);
    for (int i = 0; i < n; i++) midi_parser_feed(&p, b[i], tk[i]);
    if (stray) *stray = p.stray_data;
}

typedef struct {
    const char *name;
    int  route_a, route_b;          /* bitmask: 1 IN A, 2 IN B, 4 generator */
    int  msgs_a, msgs_b;
    int  idle_a, idle_b;            /* per mille of byte-time slots left idle */
    int  bigsx_pm_a;
    int  gen_pm;                    /* generator requests per mille per byte time */
    bool dense_gen;
    bool expect_no_drops;
    bool expect_identical;          /* OUT = IN A byte for byte, same byte time */
    bool msg_gaps;                  /* idle only between messages: the bytes of a message are
                                       back to back, as real senders do (else idle per byte) */
    int  flip_every;                /* switch to a random routing preset every N byte times */
} merge2_t;

static uint8_t  s_in2[ME_NUM_INS][STREAM_MAX];
static uint8_t  s_in2_start[ME_NUM_INS][STREAM_MAX];
static uint8_t  s_msg_route[ME_NUM_INS][MAXMSG];   /* outputs routed when each message started */
static uint32_t s_in2_tick[ME_NUM_INS][STREAM_MAX];
static uint32_t s_out_tick[ME_NUM_OUTS][STREAM_MAX * 2];

/* realtime enqueue tracking: (tick, rt bytes queued ahead) */
#define RTT_MAX 400000
static uint32_t s_rtt_tick[ME_NUM_OUTS][RTT_MAX], s_rtt_ahead[ME_NUM_OUTS][RTT_MAX];
static int s_rtt_h[ME_NUM_OUTS], s_rtt_t[ME_NUM_OUTS];
static uint32_t s_rh_seen[ME_NUM_OUTS];

static void rt_track(uint32_t tick) {
    for (int p = 0; p < ME_NUM_OUTS; p++) {
        const me_out_t *o = &g_me.out[p];
        for (; s_rh_seen[p] != o->rh; s_rh_seen[p]++) {
            if (s_rtt_h[p] < RTT_MAX) {
                s_rtt_tick[p][s_rtt_h[p]] = tick;
                s_rtt_ahead[p][s_rtt_h[p]] = s_rh_seen[p] - o->rt_t;
                s_rtt_h[p]++;
            }
        }
    }
}

static void run_merge2(const merge2_t *c) {
    printf("engine merge (2 inputs): %s\n", c->name);
    eng_reset_route(c->route_a, c->route_b);
    streamcfg_t ca = SC_A;
    ca.bigsx_pm = c->bigsx_pm_a;
    int n[ME_NUM_INS];
    memset(s_in2_start, 0, sizeof s_in2_start);
    n[0] = build_stream2(s_in2[0], s_in2_start[0], STREAM_MAX, c->msgs_a, &ca);
    n[1] = build_stream2(s_in2[1], s_in2_start[1], STREAM_MAX, c->msgs_b, &SC_B);
    const int idle[ME_NUM_INS] = { c->idle_a, c->idle_b };

    msglist_t gen_expect = ml_new();
    uint32_t gen_rt_ok = 0;
    int outn[ME_NUM_OUTS] = { 0, 0 };
    int pos[ME_NUM_INS] = { 0, 0 };
    int msgi[ME_NUM_INS] = { 0, 0 }, rt_exp[ME_NUM_OUTS] = { 0, 0 }, flips = 0;
    uint32_t tick = 0, maxq = 0, rt_delay_max = 0, rt_ahead_max = 0;
    int rt_bad = 0;
    memset(s_rtt_h, 0, sizeof s_rtt_h);
    memset(s_rtt_t, 0, sizeof s_rtt_t);
    memset(s_rh_seen, 0, sizeof s_rh_seen);

    while (pos[0] < n[0] || pos[1] < n[1] || !me_idle(&g_me)) {
        uint64_t t = (uint64_t)tick * 320u;
        if (c->flip_every && tick % (uint32_t)c->flip_every == 0 && tick) {
            route_preset_apply(&g_set, (int)(rnd() % ROUTE_PRESET_COUNT));
            flips++;
        }
        for (int i = 0; i < ME_NUM_INS; i++) {
            if (pos[i] >= n[i]) continue;
            bool may_idle = !c->msg_gaps || s_in2_start[i][pos[i]];
            if (may_idle && (int)(rnd() % 1000) < idle[i]) continue;
            uint8_t b = s_in2[i][pos[i]];
            uint8_t routed = (uint8_t)((g_set.src[0][i] ? 1 : 0) | (g_set.src[1][i] ? 2 : 0));
            if (b >= 0xF8) {
                for (int p = 0; p < ME_NUM_OUTS; p++) if ((routed >> p) & 1) rt_exp[p]++;
            } else if (s_in2_start[i][pos[i]] && msgi[i] < MAXMSG) {
                s_msg_route[i][msgi[i]++] = routed;
            }
            s_in2_tick[i][pos[i]] = tick;
            me_input_byte(&g_me, i, s_in2[i][pos[i]++], t);
            rt_track(tick);
        }
        bool feeding = pos[0] < n[0] || pos[1] < n[1];
        if (feeding && (int)(rnd() % 1000) < c->gen_pm) {
            uint8_t g[ME_GEN_MAX_LEN];
            int gl = make_gen(g);
            if (me_gen_push(&g_me, g, (uint8_t)gl)) {
                midi_parser_t p;
                midi_parser_init(&p, collect, &gen_expect);
                for (int k = 0; k < gl; k++) midi_parser_feed(&p, g[k], 0);
            }
            if (c->dense_gen && me_gen_rt_push(&g_me, 0xFE)) gen_rt_ok++;
        }
        if (feeding && c->gen_pm && rnd() % 200 == 0 && me_gen_rt_push(&g_me, 0xFE)) gen_rt_ok++;
        me_service(&g_me, t);
        rt_track(tick);
        for (int p = 0; p < ME_NUM_OUTS; p++) {
            uint8_t b;
            if (me_out_pending(&g_me, p) > maxq) maxq = me_out_pending(&g_me, p);
            if (me_out_pop(&g_me, p, &b)) {
                s_out[p][outn[p]] = b;
                s_out_tick[p][outn[p]++] = tick;
                if (b >= 0xF8) {
                    if (s_rtt_t[p] >= s_rtt_h[p]) { rt_bad++; continue; }
                    uint32_t d = tick - s_rtt_tick[p][s_rtt_t[p]];
                    uint32_t ah = s_rtt_ahead[p][s_rtt_t[p]];
                    s_rtt_t[p]++;
                    if (d > ah) rt_bad++;     /* only other realtime may go first */
                    if (d > rt_delay_max) rt_delay_max = d;
                    if (ah > rt_ahead_max) rt_ahead_max = ah;
                }
            }
        }
        tick++;
        if (tick > 20u * 1000u * 1000u) break;
    }

    /* expected per-input message lists (realtime excluded) */
    msglist_t in_msgs[ME_NUM_INS];
    int in_rt[ME_NUM_INS] = { 0, 0 };
    for (int i = 0; i < ME_NUM_INS; i++) {
        msglist_t all = ml_new();
        uint32_t stray;
        parse_ticks(s_in2[i], s_in2_tick[i], n[i], &all, &stray);
        CHECK(stray == 0);
        in_msgs[i] = ml_new();
        for (int k = 0; k < all.n; k++) {
            if (all.m[k].status >= 0xF8) in_rt[i]++;
            else in_msgs[i].m[in_msgs[i].n++] = all.m[k];
        }
        ml_free(&all);
        CHECK(msgi[i] == in_msgs[i].n);
    }

    for (int p = 0; p < ME_NUM_OUTS; p++) {
        int route = p ? c->route_b : c->route_a;
        /* what this output should carry from each input: the messages that
         * started while the input was routed here */
        msglist_t exp[ME_NUM_INS];
        for (int s = 0; s < ME_NUM_INS; s++) {
            exp[s] = ml_new();
            for (int k = 0; k < in_msgs[s].n; k++)
                if ((s_msg_route[s][k] >> p) & 1) exp[s].m[exp[s].n++] = in_msgs[s].m[k];
        }
        msglist_t om = ml_new();
        uint32_t stray;
        parse_ticks(s_out[p], s_out_tick[p], outn[p], &om, &stray);
        int ii[ME_NUM_INS] = { 0, 0 }, got[ME_NUM_INS] = { 0, 0 };
        int gi = 0, rt_in[ME_NUM_INS] = { 0, 0 }, fe = 0, bad = 0;
        uint32_t lat_max[ME_NUM_INS] = { 0, 0 };
        for (int k = 0; k < om.n; k++) {
            const midi_msg_t *m = &om.m[k];
            if (m->status == 0xF8) { rt_in[0]++; continue; }
            if (m->status == 0xFD) { rt_in[1]++; continue; }
            if (m->status == 0xFE) { fe++; continue; }
            int s = msg_source(m);
            if (s == 2) {
                if (gi >= gen_expect.n || !msg_eq(m, &gen_expect.m[gi])) bad++;
                gi++;
                continue;
            }
            if (s < 0) {
                if (bad < 3) printf("  port %c: unexpected message status %02X\n", 'A' + p, m->status);
                bad++;
                continue;
            }
            /* next message from input s must be a later message of s (whole
             * messages may have been dropped, never reordered or altered) */
            while (ii[s] < exp[s].n && !msg_eq(m, &exp[s].m[ii[s]])) ii[s]++;
            if (ii[s] >= exp[s].n) {
                if (bad < 3) {
                    char a[32];
                    decode(m, a);
                    printf("  port %c: message from IN %c out of order / altered: \"%s\"\n", 'A' + p, 'A' + s, a);
                }
                bad++;
                continue;
            }
            uint32_t lat = (uint32_t)(m->t_us - exp[s].m[ii[s]].t_us);
            if (lat > lat_max[s]) lat_max[s] = lat;
            ii[s]++;
            got[s]++;
        }
        CHECK(bad == 0);
        CHECK(stray == 0);
        CHECK(g_me.out[p].drops == 0);
        CHECK(gi == ((route & 4) ? gen_expect.n : 0));
        CHECK(fe == ((route & 4) ? (int)(gen_rt_ok - g_me.out[p].gen_shed) : 0));
        uint32_t dropped_total = 0;
        for (int s = 0; s < ME_NUM_INS; s++) {
            uint32_t drops = g_me.out[p].merge_drops[s];
            dropped_total += drops;
            CHECK((uint32_t)got[s] + drops == (uint32_t)exp[s].n);   /* every loss counted, whole */
            CHECK(rt_in[s] <= in_rt[s]);
        }
        /* input realtime: all of it unless shed (merge overload only) */
        CHECK(rt_in[0] + rt_in[1] + (int)g_me.out[p].rt_shed == rt_exp[p]);
        if (c->bigsx_pm_a && (route & 1))
            CHECK(g_me.out[p].merge_drops[ME_IN_A] == 0);   /* the dumping input never loses */
        if (c->expect_no_drops) {
            CHECK(dropped_total == 0);
            CHECK(g_me.out[p].rt_shed == 0);
        }
        printf("  port %c: %d bytes; IN A %d/%d msgs (lost %u, max lat %.1f ms); IN B %d/%d (lost %u, max lat %.1f ms);"
               " gen %d; RT %d+%d (+%d gen)\n",
               'A' + p, outn[p], got[0], exp[0].n, (unsigned)g_me.out[p].merge_drops[0],
               lat_max[0] * 0.32, got[1], exp[1].n, (unsigned)g_me.out[p].merge_drops[1],
               lat_max[1] * 0.32, gi, rt_in[0], rt_in[1], fe);

        if (c->expect_identical) {
            /* single input fast path: byte-identical, each byte in its own byte time */
            int ok = outn[p] == n[0];
            for (int k = 0; ok && k < n[0]; k++)
                ok = s_out[p][k] == s_in2[0][k] && s_out_tick[p][k] == s_in2_tick[0][k];
            CHECK(ok);
        }
        ml_free(&om);
        ml_free(&exp[0]);
        ml_free(&exp[1]);
    }
    if (flips) printf("  %d routing changes mid-stream\n", flips);
    CHECK(rt_bad == 0);
    CHECK(g_me.in[0].st.orphan_data == 0 && g_me.in[1].st.orphan_data == 0);
    CHECK(g_me.stats.stalls == 0);
    printf("  inputs: A %d bytes / %d msgs, B %d bytes / %d msgs; gen %d msgs, %u RT;"
           " max TX backlog %u bytes (%.1f ms); realtime max delay %u byte times (queued behind %u RT)\n",
           n[0], in_msgs[0].n, n[1], in_msgs[1].n, gen_expect.n, (unsigned)gen_rt_ok,
           (unsigned)maxq, maxq * 0.32, (unsigned)rt_delay_max, (unsigned)rt_ahead_max);
    CHECK(maxq <= ME_MERGE_BACKLOG_MAX + 2u * ME_GEN_MAX_LEN + 16u ||
          maxq <= ME_LANE_SIZE + ME_MERGE_BACKLOG_MAX + 16u);
    ml_free(&in_msgs[0]);
    ml_free(&in_msgs[1]);
    ml_free(&gen_expect);
}

/* IN B overflow: IN A sends 10 kB sysex dumps back to back on a merged
 * output while IN B streams. Light IN B traffic fits in its lane and loses
 * nothing; heavy IN B traffic loses whole IN B messages only, all counted.
 * Checked by run_merge2's invariants. */
static void test_engine_merge2(void) {
    static const merge2_t runs[] = {
        /* name                                                       rA rB  msgsA  msgsB idleA idleB bigsx gen dense nodrop ident gaps  flip */
        { "fast path: IN A thru to both, IN B monitored only (dense)", 1, 1, 30000, 30000,   0,   0,  0,  0, false, true,  true,  false, 0 },
        { "default routing, dense IN A + IN B + heavy generator",      5, 5, 30000, 30000,   0,   0,  0, 50, true,  true,  false, false, 0 },
        { "split A->A, B->B, both dense + generator",                  5, 6, 30000, 30000,   0,   0,  0, 30, true,  true,  false, false, 0 },
        { "B->A, B->B, IN A dense unrouted",                           2, 2, 20000, 20000,   0, 200,  0,  0, false, true,  false, false, 0 },
        { "merge both, 45% + 45% (bytes trickled) + generator",       7, 7, 15000, 15000, 550, 550,  0, 10, false, true,  false, false, 0 },
        { "merge both, ~45% + ~45% (whole messages) + generator",     7, 7, 15000, 15000, 940, 940,  0, 10, false, true,  false, true, 0 },
        { "merge both, 2-10 kB dumps on A, IN B ~6% + generator",      7, 7,  3000,  3000, 995, 995, 15,  3, false, true,  false, true, 0 },
        { "merge to A, 2-10 kB dumps on A, IN B ~35% (B overflows)",   3, 2,  3000, 15000, 995, 960, 15,  0, false, false, false, true, 0 },
        { "routing preset changes every 50 ms, ~40% + ~40% + generator", 7, 7, 15000, 15000, 950, 950, 0, 10, false, true, false, true, 156 },
        { "routing preset changes every 3 ms (mid-message), dense + gen", 4, 4, 15000, 15000, 300, 300, 0, 20, false, false, false, false, 9 },
        { "merge both, both 100% + heavy generator (overload)",        7, 7, 30000, 30000,   0,   0,  0, 50, true,  false, false, false, 0 },
        { "merge both, ~70% + ~70% + very heavy generator (overload)", 7, 7, 30000, 30000, 850, 850,  0,300, true,  false, false, true, 0 },
    };
    for (unsigned k = 0; k < sizeof runs / sizeof runs[0]; k++) run_merge2(&runs[k]);
}

int main(void) {
    test_parser();
    test_decode();
    test_msc();
    test_m2();
    test_engine_directed();
    test_engine_two_inputs();
    test_engine_monitor();
    test_routing_ui();
    run_merge("sparse input, moderate generator", 20000, 50, 20, false);
    run_merge("dense input (100%), heavy generator", 40000, 0, 50, true);
    run_merge("70% input, very heavy generator", 40000, 30, 300, true);
    test_engine_merge2();
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
