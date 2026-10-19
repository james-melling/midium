/*
 * Host-side unit tests for the MIDI Show Control decoder / builder
 * (src/msc_decode.c). Vectors follow MSC 1.1.1; where the spec gives an
 * example (cue data in 3.1, the 2PC cue sequence in 6.9) it is used.
 *
 * MSC_SHOW=1 ./test_midium   also prints mock OLED screens.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_common.h"
#include "msc_decode.h"
#include "midi_decode.h"
#include "midi_parser.h"

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */
static bool dec(const uint8_t *b, size_t n, msc_msg_t *d) { return msc_decode(b, n, n, d); }

#define DEC(d, ...) do { const uint8_t b_[] = { __VA_ARGS__ }; \
        CHECK(dec(b_, sizeof b_, &(d))); } while (0)

static const char *sum(const msc_msg_t *d, unsigned width) {
    static char b[MIDI_TEXT_MAX + 8];
    msc_summary(d, width, b, sizeof b);
    return b;
}

static midi_msg_t g_last;
static int g_nmsg;
static void grab(const midi_msg_t *m, void *ctx) { (void)ctx; g_last = *m; g_nmsg++; }

static void parse(const uint8_t *b, size_t n) {
    midi_parser_t p;
    g_nmsg = 0;
    midi_parser_init(&p, grab, NULL);
    for (size_t i = 0; i < n; i++) midi_parser_feed(&p, b[i], 1000u * i);
}

/* detail page capture */
#define DMAX 128
static char d_lines[DMAX][64];
static int  d_cls[DMAX];
static int  d_n;
static void dcb(const char *t, msg_class_t c, void *ctx) {
    (void)ctx;
    if (d_n < DMAX) { snprintf(d_lines[d_n], sizeof d_lines[0], "%s", t); d_cls[d_n] = c; }
    d_n++;
}
static unsigned detail(const midi_msg_t *m, unsigned cols) {
    d_n = 0;
    return sysex_detail(m, cols, dcb, NULL);
}
static bool has_line(const char *s) {
    for (int i = 0; i < d_n && i < DMAX; i++) if (strcmp(d_lines[i], s) == 0) return true;
    return false;
}
static bool lines_fit(unsigned cols) {
    for (int i = 0; i < d_n && i < DMAX; i++) if (strlen(d_lines[i]) > cols) return false;
    return true;
}

/* mock OLED screen: header + rows-1 lines, framed */
static void show_screen(const char *hdr, const char lines[][64], int n, int cols, int rows) {
    printf("    +%.*s+\n", cols, "----------------------------------------");
    printf("    |%-*.*s|\n", cols, cols, hdr);
    for (int r = 0; r < rows - 1; r++) printf("    |%-*.*s|\n", cols, cols, r < n ? lines[r] : "");
    printf("    +%.*s+\n", cols, "----------------------------------------");
}

static msc_time_t tc(uint8_t type, uint8_t h, uint8_t m, uint8_t s, uint8_t f, uint8_t sf) {
    msc_time_t t;
    memset(&t, 0, sizeof t);
    t.present = true; t.type = type; t.hours = h; t.minutes = m; t.seconds = s; t.frames = f; t.subframes = sf;
    return t;
}

/* ------------------------------------------------------------------------ */
/* tables: formats, commands, devices                                        */
/* ------------------------------------------------------------------------ */
static void test_tables(void) {
    printf("msc tables\n");
    static const struct { uint8_t c; const char *name; } F[] = {
        { 0x01, "Lighting (General Category)" }, { 0x02, "Moving Lights" }, { 0x03, "Color Changers" },
        { 0x04, "Strobes" }, { 0x05, "Lasers" }, { 0x06, "Chasers" },
        { 0x10, "Sound (General Category)" }, { 0x11, "Music" }, { 0x12, "CD Players" },
        { 0x13, "EPROM Playback" }, { 0x14, "Audio Tape Machines" }, { 0x15, "Intercoms" },
        { 0x16, "Amplifiers" }, { 0x17, "Audio Effects Devices" }, { 0x18, "Equalizers" },
        { 0x20, "Machinery (General Cat.)" }, { 0x21, "Rigging" }, { 0x22, "Flys" }, { 0x23, "Lifts" },
        { 0x24, "Turntables" }, { 0x25, "Trusses" }, { 0x26, "Robots" }, { 0x27, "Animation" },
        { 0x28, "Floats" }, { 0x29, "Breakaways" }, { 0x2A, "Barges" },
        { 0x30, "Video (General Category)" }, { 0x31, "Video Tape Machines" },
        { 0x32, "Video Cassette Machines" }, { 0x33, "Video Disc Players" }, { 0x34, "Video Switchers" },
        { 0x35, "Video Effects" }, { 0x36, "Video Character Generators" }, { 0x37, "Video Still Stores" },
        { 0x38, "Video Monitors" },
        { 0x40, "Projection (General)" }, { 0x41, "Film Projectors" }, { 0x42, "Slide Projectors" },
        { 0x43, "Video Projectors" }, { 0x44, "Dissolvers" }, { 0x45, "Shutter Controls" },
        { 0x50, "Process Control (Gen.)" }, { 0x51, "Hydraulic Oil" }, { 0x52, "H2O" }, { 0x53, "CO2" },
        { 0x54, "Compressed Air" }, { 0x55, "Natural Gas" }, { 0x56, "Fog" }, { 0x57, "Smoke" },
        { 0x58, "Cracked Haze" },
        { 0x60, "Pyro (General Category)" }, { 0x61, "Fireworks" }, { 0x62, "Explosions" },
        { 0x63, "Flame" }, { 0x64, "Smoke pots" }, { 0x7F, "All-types" },
    };
    int defined = 0;
    for (unsigned c = 0; c < 128; c++) if (msc_format_name((uint8_t)c)) defined++;
    CHECK(defined == (int)(sizeof F / sizeof F[0]));
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        CHECK(msc_format_name(F[i].c) && strcmp(msc_format_name(F[i].c), F[i].name) == 0);
        CHECK(msc_format_abbr(F[i].c) && strlen(msc_format_abbr(F[i].c)) <= 4);
    }
    CHECK_STR(msc_format_abbr(0x01), "LTG");
    CHECK_STR(msc_format_abbr(0x10), "SND");
    CHECK_STR(msc_format_abbr(0x20), "MACH");
    CHECK_STR(msc_format_abbr(0x30), "VID");
    CHECK_STR(msc_format_abbr(0x60), "PYRO");
    CHECK_STR(msc_format_abbr(0x7F), "ALL");
    CHECK(msc_format_name(0x00) == NULL && msc_format_name(0x07) == NULL && msc_format_name(0x65) == NULL);

    /* abbreviations unique; generator list is a permutation of the table */
    CHECK(msc_gen_format_count() == sizeof F / sizeof F[0]);
    bool seen[128] = { false };
    bool uniq = true, perm = true;
    for (unsigned i = 0; i < msc_gen_format_count(); i++) {
        uint8_t f = msc_gen_format(i);
        if (!msc_format_name(f) || seen[f]) perm = false;
        seen[f] = true;
        for (unsigned j = 0; j < i; j++)
            if (strcmp(msc_format_abbr(f), msc_format_abbr(msc_gen_format(j))) == 0) uniq = false;
    }
    CHECK(uniq);
    CHECK(perm);
    CHECK(msc_gen_format(0) == 0x7F && msc_gen_format(1) == 0x01 && msc_gen_format(2) == 0x10);

    static const struct { uint8_t c; const char *name; } C[] = {
        { 0x01, "GO" }, { 0x02, "STOP" }, { 0x03, "RESUME" }, { 0x04, "TIMED_GO" }, { 0x05, "LOAD" },
        { 0x06, "SET" }, { 0x07, "FIRE" }, { 0x08, "ALL_OFF" }, { 0x09, "RESTORE" }, { 0x0A, "RESET" },
        { 0x0B, "GO_OFF" }, { 0x10, "GO/JAM_CLOCK" }, { 0x11, "STANDBY_+" }, { 0x12, "STANDBY_-" },
        { 0x13, "SEQUENCE_+" }, { 0x14, "SEQUENCE_-" }, { 0x15, "START_CLOCK" }, { 0x16, "STOP_CLOCK" },
        { 0x17, "ZERO_CLOCK" }, { 0x18, "SET_CLOCK" }, { 0x19, "MTC_CHASE_ON" }, { 0x1A, "MTC_CHASE_OFF" },
        { 0x1B, "OPEN_CUE_LIST" }, { 0x1C, "CLOSE_CUE_LIST" }, { 0x1D, "OPEN_CUE_PATH" },
        { 0x1E, "CLOSE_CUE_PATH" }, { 0x20, "STANDBY" }, { 0x21, "STANDING_BY" }, { 0x22, "GO_2PC" },
        { 0x23, "COMPLETE" }, { 0x24, "CANCEL" }, { 0x25, "CANCELLED" }, { 0x26, "ABORT" },
    };
    defined = 0;
    for (unsigned c = 0; c < 128; c++) if (msc_command_name((uint8_t)c)) defined++;
    CHECK(defined == (int)(sizeof C / sizeof C[0]));
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        CHECK(msc_command_name(C[i].c) && strcmp(msc_command_name(C[i].c), C[i].name) == 0);
        CHECK(msc_command_abbr(C[i].c) && strlen(msc_command_abbr(C[i].c)) <= 6);
    }

    char b[24];
    msc_device_short(0x00, b, sizeof b); CHECK_STR(b, "0");
    msc_device_short(0x6F, b, sizeof b); CHECK_STR(b, "111");
    msc_device_short(0x70, b, sizeof b); CHECK_STR(b, "G1");
    msc_device_short(0x7E, b, sizeof b); CHECK_STR(b, "G15");
    msc_device_short(0x7F, b, sizeof b); CHECK_STR(b, "ALL");
    msc_device_long(0x01, b, sizeof b);  CHECK_STR(b, "01h (1)");
    msc_device_long(0x75, b, sizeof b);  CHECK_STR(b, "75h Group 6");
    msc_device_long(0x7F, b, sizeof b);  CHECK_STR(b, "7Fh All-call");
}

/* ------------------------------------------------------------------------ */
/* basic decode + summary                                                    */
/* ------------------------------------------------------------------------ */
static void test_basic(void) {
    printf("msc decode basics\n");
    msc_msg_t d;

    /* the summary formats from the brief */
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0xF7);
    CHECK(d.dev == 0x7F && d.fmt == 0x01 && d.cmd == 0x01 && d.warn == 0);
    CHECK(d.has_cue && !d.has_list && !d.has_path);
    CHECK_STR(d.cue, "12.5");
    CHECK_STR(sum(&d, 21), "MSC LTG GO Q12.5");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x10, 0x02, 0xF7);
    CHECK_STR(sum(&d, 21), "MSC SND STOP");
    CHECK(!d.has_cue && d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x01, '1', 0x00, '2', 0xF7);
    CHECK_STR(sum(&d, 21), "MSC ALL GO Q1 L2");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x03, 0x00, 0x7F, 0x01, 0xF7);
    CHECK_STR(sum(&d, 21), "MSC LTG SET 3=255");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0x61, 0x02, 0x03, 0x00, 0x00, '4', 0xF7);
    CHECK_STR(sum(&d, 21), "MSC TGO Q4 01:02:03");
    CHECK_STR(sum(&d, 26), "MSC LTG TGO Q4 01:02:03:00");

    /* device suffix, width adaptation */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0x00, '1', 0xF7);
    CHECK_STR(sum(&d, 21), "MSC LTG GO Q12.5 L1");
    CHECK_STR(sum(&d, 26), "MSC LTG GO Q12.5 L1 @1");
    CHECK_STR(sum(&d, 11), "MSC GO Q12~");
    CHECK(strlen(sum(&d, 3)) <= 3);
    DEC(d, 0xF0, 0x7F, 0x72, 0x02, 0x10, 0x01, '7', 0xF7);
    CHECK_STR(sum(&d, 21), "MSC SND GO Q7 @G3");

    /* spec 3.1 example: cue 235.6 list 36.6 path 59 */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01,
        0x32, 0x33, 0x35, 0x2E, 0x36, 0x00, 0x33, 0x36, 0x2E, 0x36, 0x00, 0x35, 0x39, 0xF7);
    CHECK(d.warn == 0);
    CHECK_STR(d.cue, "235.6");
    CHECK_STR(d.list, "36.6");
    CHECK_STR(d.path, "59");
    CHECK_STR(sum(&d, 26), "MSC GO Q235.6 L36.6 P59");
    CHECK_STR(sum(&d, 21), "MSC GO Q235.6 L36.6");
    /* section 2.6 example of a many-subsection cue number */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '2', '3', '5', '.', '3', '2', '.', '7', '.', '8', '.', '6', '5', '4', 0xF7);
    CHECK_STR(d.cue, "235.32.7.8.654");
    CHECK(d.warn == 0);

    /* not MSC */
    const uint8_t mmc[] = { 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7 };
    CHECK(!dec(mmc, sizeof mmc, &d));
    const uint8_t sh[] = { 0xF0, 0x7F, 0x01 };
    CHECK(!dec(sh, sizeof sh, &d));
    const uint8_t other[] = { 0xF0, 0x7E, 0x01, 0x02, 0x01, 0x01, 0xF7 };
    CHECK(!dec(other, sizeof other, &d));
    CHECK(!msc_decode(NULL, 0, 0, &d));
}

/* ------------------------------------------------------------------------ */
/* every command, built and decoded                                          */
/* ------------------------------------------------------------------------ */
static void test_all_commands(void) {
    printf("msc every command\n");
    for (unsigned c = 0; c < 128; c++) {
        if (!msc_command_name((uint8_t)c)) continue;
        msc_time_t t = tc(MSC_TC_30, 1, 2, 3, 4, 50);
        msc_build_t b;
        memset(&b, 0, sizeof b);
        b.dev = 0x05; b.fmt = 0x22; b.cmd = (uint8_t)c;
        b.cue = "28.1"; b.list = "3"; b.path = "2";
        b.time = &t; b.ctrl = 1000; b.value = 12345; b.macro = 99;
        b.seq = 4321; b.d[0] = 1; b.d[1] = 2; b.d[2] = 3; b.d[3] = 4;
        b.status = c == MSC_CANCELLED ? 0x8010 : 0x1004;
        uint8_t m[MSC_MAX_LEN];
        size_t n = msc_build(&b, m, sizeof m);
        CHECK(n >= 7 && m[0] == 0xF0 && m[n - 1] == 0xF7);
        msc_msg_t d;
        CHECK(dec(m, n, &d));
        if (d.warn) printf("  cmd %02X warn %04X\n", c, (unsigned)d.warn);
        CHECK(d.warn == 0 && d.has_cmd && d.cmd == c && d.fmt == 0x22 && d.dev == 0x05);
        char s[MIDI_TEXT_MAX];
        msc_summary(&d, 21, s, sizeof s);
        CHECK(strncmp(s, "MSC", 3) == 0 && strlen(s) <= 21);
        CHECK(strchr(s, '!') == NULL);

        bool cue3 = c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x05 || c == 0x0B ||
                    c == 0x10 || c == 0x20 || c == 0x21 || c == 0x22 || c == 0x23 || c == 0x24;
        bool listonly = (c >= 0x11 && c <= 0x1C);
        bool pathonly = c == 0x1D || c == 0x1E;
        bool timed = c == 0x04 || c == 0x06 || c == 0x18 || c == 0x21;
        if (cue3) {
            CHECK(d.has_cue && d.has_list && d.has_path);
            CHECK(strcmp(d.cue, "28.1") == 0 && strcmp(d.list, "3") == 0 && strcmp(d.path, "2") == 0);
        } else if (listonly) {
            CHECK(!d.has_cue && d.has_list && !d.has_path && strcmp(d.list, "3") == 0);
        } else if (pathonly) {
            CHECK(!d.has_cue && !d.has_list && d.has_path && strcmp(d.path, "2") == 0);
        } else {
            CHECK(!d.has_cue && !d.has_list && !d.has_path);
        }
        CHECK(d.time.present == timed);
        if (timed) CHECK(d.time.hours == 1 && d.time.minutes == 2 && d.time.seconds == 3 &&
                         d.time.frames == 4 && d.time.subframes == 50 && d.time.type == MSC_TC_30);
        CHECK(d.has_ctrl == (c == MSC_SET));
        if (c == MSC_SET) CHECK(d.ctrl == 1000 && d.value == 12345);
        CHECK(d.has_macro == (c == MSC_FIRE));
        if (c == MSC_FIRE) CHECK(d.macro == 99);
        CHECK(d.is_2pc == (c >= 0x20 && c <= 0x26));
        if (d.is_2pc) {
            CHECK(d.has_chk && d.chk_checked && d.chk_ok && !d.chk_alt);
            CHECK(d.has_seq && d.seq == 4321);
            CHECK(d.has_d == (c == MSC_STANDBY || c == MSC_GO_2PC));
            if (d.has_d) CHECK(d.d[0] == 1 && d.d[1] == 2 && d.d[2] == 3 && d.d[3] == 4);
            CHECK(d.has_status == (c == MSC_CANCELLED || c == MSC_ABORT));
            if (c == MSC_CANCELLED) CHECK(d.status == 0x8010);
            if (c == MSC_ABORT) CHECK(d.status == 0x1004);
        }
        /* minimum forms: optional data omitted */
        memset(&b, 0, sizeof b);
        b.dev = 0x7F; b.fmt = 0x7F; b.cmd = (uint8_t)c; b.time = &t; b.seq = 1;
        if (c == MSC_LOAD || c == MSC_STANDBY || c == MSC_GO_2PC || c == MSC_CANCEL) b.cue = "1";
        if (c == MSC_OPEN_CUE_LIST || c == MSC_CLOSE_CUE_LIST) b.list = "1";
        if (c == MSC_OPEN_CUE_PATH || c == MSC_CLOSE_CUE_PATH) b.path = "1";
        if (c == MSC_SET) b.time = NULL;
        if (c == MSC_ABORT) b.status = 0x0000;
        n = msc_build(&b, m, sizeof m);
        CHECK(dec(m, n, &d) && d.warn == 0);
        if (c == MSC_SET) CHECK(n == 11 && !d.time.present);
    }

    /* undefined command codes */
    static const uint8_t undef[] = { 0x0C, 0x0F, 0x1F, 0x27, 0x40, 0x7F };
    for (size_t i = 0; i < sizeof undef; i++) {
        const uint8_t m[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, undef[i], '1', 0xF7 };
        msc_msg_t d;
        CHECK(dec(m, sizeof m, &d) && (d.warn & MSC_W_CMD_UNKNOWN));
    }
    msc_msg_t d;
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x1F, 0xF7);
    CHECK_STR(sum(&d, 21), "MSC LTG C1F @1!");
    /* extension sets: command_format 00 / command 00 */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x00, 0x01, 0x01, 0xF7);
    CHECK((d.warn & MSC_W_EXTENSION) && !(d.warn & MSC_W_FMT_UNKNOWN));
    CHECK_STR(sum(&d, 21), "MSC EXT C01 @1!");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x00, 0x01, 0xF7);
    CHECK(d.warn == MSC_W_EXTENSION);
    /* undefined formats */
    static const uint8_t uf[] = { 0x07, 0x0F, 0x19, 0x2B, 0x39, 0x46, 0x59, 0x65, 0x70, 0x7E };
    for (size_t i = 0; i < sizeof uf; i++) {
        const uint8_t m[] = { 0xF0, 0x7F, 0x7F, 0x02, uf[i], 0x01, 0xF7 };
        CHECK(dec(m, sizeof m, &d) && d.warn == MSC_W_FMT_UNKNOWN && d.cmd == 0x01);
    }
    CHECK_STR(sum(&d, 21), "MSC F7E GO!");
}

/* ------------------------------------------------------------------------ */
/* cue fields                                                                */
/* ------------------------------------------------------------------------ */
static void test_cues(void) {
    printf("msc cue fields\n");
    msc_msg_t d;
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, 0xF7);                 /* GO, no cue */
    CHECK(!d.has_cue && d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xF7);     /* only delimiters: allowed */
    CHECK(!d.has_cue && !d.has_list && d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '5', 0x00, 0xF7);      /* delimiter before F7 */
    CHECK(d.has_cue && !d.has_list && d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '5', 0x00, '1', 0x00, 0x00, 0xF7);
    CHECK(d.has_list && !d.has_path && d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '3', '7', '.', 0xF7);  /* "37." appears in spec 5 */
    CHECK(d.warn == 0);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '.', '.', '2', 0xF7);
    CHECK(d.warn == MSC_W_Q_DOTS);
    CHECK_STR(d.cue, "1..2");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, 'A', '1', 0x05, 0xF7);
    CHECK(d.warn == MSC_W_Q_CHARS);
    CHECK_STR(d.cue, "A1?");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, 0x00, '2', 0xF7);      /* list without cue */
    CHECK(d.warn == MSC_W_Q_ORDER && !d.has_cue && d.has_list);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', 0x00, 0x00, '3', 0xF7);   /* path w/o list */
    CHECK(d.warn == MSC_W_Q_ORDER && d.has_path && !d.has_list);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0xF7);
    CHECK(d.warn == MSC_W_EXTRA);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x05, 0xF7);                 /* LOAD needs a cue */
    CHECK(d.warn == MSC_W_Q_MISSING);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x05, 0x00, '2', 0xF7);
    CHECK(d.warn == (MSC_W_Q_MISSING | MSC_W_Q_ORDER));
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x1B, 0xF7);                 /* OPEN_CUE_LIST needs Q_list */
    CHECK(d.warn == MSC_W_Q_MISSING);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x1E, '4', 0xF7);            /* CLOSE_CUE_PATH */
    CHECK(d.warn == 0 && d.has_path && !d.has_list);
    CHECK_STR(sum(&d, 21), "MSC LTG CLSPTH P4 @1");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x11, 0xF7);                 /* STANDBY_+ no list */
    CHECK(d.warn == 0 && !d.has_list);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x11, '2', 0x00, '9', 0xF7); /* STANDBY_+ extra field */
    CHECK(d.warn == MSC_W_EXTRA && d.has_list);

    /* long cue: clipped, flagged */
    uint8_t m[MSC_MAX_LEN];
    size_t n = 0;
    m[n++] = 0xF0; m[n++] = 0x7F; m[n++] = 0x01; m[n++] = 0x02; m[n++] = 0x01; m[n++] = 0x01;
    while (n < 60) m[n++] = '1';
    m[n++] = 0xF7;
    CHECK(dec(m, n, &d) && d.warn == MSC_W_Q_LONG && strlen(d.cue) == MSC_FIELD_MAX);
}

/* ------------------------------------------------------------------------ */
/* time code                                                                 */
/* ------------------------------------------------------------------------ */
static void test_time(void) {
    printf("msc time code\n");
    static const char *const rate[4] = { "24", "25", "30DF", "30" };
    static const unsigned fps[4] = { 24, 25, 30, 30 };
    msc_msg_t d;
    char s[32];
    for (uint8_t ty = 0; ty < 4; ty++) {
        /* TIMED_GO 01:02:03:04.50, cue 4 */
        const uint8_t m[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, (uint8_t)((ty << 5) | 1), 2, 3, 4, 50, '4', 0xF7 };
        CHECK(dec(m, sizeof m, &d) && d.warn == 0);
        CHECK(d.time.present && d.time.type == ty && d.time.hours == 1 && d.time.minutes == 2 &&
              d.time.seconds == 3 && d.time.frames == 4 && d.time.subframes == 50 && !d.time.is_status);
        CHECK_STR(msc_tc_rate_name(ty), rate[ty]);
        CHECK(msc_tc_fps(ty) == fps[ty]);
        msc_time_str(&d.time, true, true, s, sizeof s);
        CHECK_STR(s, "01:02:03:04.50");
        CHECK_STR(sum(&d, 21), "MSC TGO Q4 01:02:03");
        CHECK_STR(sum(&d, 26), "MSC LTG TGO Q4 01:02:03:04");
        /* highest legal frame number for the rate, and one past it */
        const uint8_t ok[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, (uint8_t)((ty << 5) | 23), 59, 59, (uint8_t)(fps[ty] - 1), 99, 0xF7 };
        CHECK(dec(ok, sizeof ok, &d) && d.warn == 0 && d.time.hours == 23);
        const uint8_t bad[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, (uint8_t)(ty << 5), 0, 0, (uint8_t)fps[ty], 0, 0xF7 };
        CHECK(dec(bad, sizeof bad, &d) && d.warn == MSC_W_TIME);
    }
    /* sign and colour-frame bits */
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0x20, 0x40, 0x05, 0x40, 0x00, 0xF7);
    CHECK(d.warn == 0 && d.time.negative && d.time.colour_frame && d.time.minutes == 0 && d.time.type == MSC_TC_25);
    msc_time_str(&d.time, true, false, s, sizeof s);
    CHECK_STR(s, "-00:00:05:00");
    CHECK_STR(sum(&d, 26), "MSC LTG TGO -00:00:05:00");
    /* range / reserved-bit errors */
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 24, 0, 0, 0, 0, 0xF7);     /* 24 h */
    CHECK(d.warn == MSC_W_TIME);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0, 60, 0, 0, 0, 0xF7);     /* 60 min */
    CHECK(d.warn == MSC_W_TIME);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0, 0, 0x40, 0, 0, 0xF7);   /* k bit */
    CHECK(d.warn == MSC_W_TIME);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0, 0, 0, 0, 100, 0xF7);    /* 100 subframes */
    CHECK(d.warn == MSC_W_TIME);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0, 0, 0, 0x20, 0x40, 0xF7); /* {st} form in TIMED_GO */
    CHECK(d.warn == MSC_W_TIME && d.time.is_status);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x04, 0, 0, 0, 0xF7);           /* time cut short */
    CHECK(d.warn == MSC_W_SHORT && !d.time.present);
    /* SET_CLOCK allows the status form: estimated + invalid + field bits */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x18, 0x41, 0, 0, 0x20, 0x70, '2', 0xF7);
    CHECK(d.warn == 0 && d.time.is_status && d.time.status == 0x70 && d.time.type == MSC_TC_30DF);
    CHECK(d.has_list && strcmp(d.list, "2") == 0);
    CHECK_STR(sum(&d, 40), "MSC SND CLKSET 01:00:00:00 L2 @1");
    CHECK_STR(sum(&d, 26), "MSC CLKSET 01:00:00:00 L2");
    CHECK_STR(sum(&d, 21), "MSC CLKSET 01:00:00~");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x18, 0, 0, 0, 0x20, 0x01, 0xF7); /* st reserved bits */
    CHECK(d.warn == MSC_W_TIME);
    /* builder round trip of every bit */
    msc_time_t t = tc(MSC_TC_30DF, 17, 33, 44, 29, 0);
    t.negative = true; t.colour_frame = true; t.is_status = true; t.status = 0x50;
    uint8_t raw[5];
    msc_time_encode(&t, raw);
    CHECK(raw[0] == 0x51 && raw[1] == 0x61 && raw[2] == 44 && raw[3] == 0x7D && raw[4] == 0x50);
}

/* ------------------------------------------------------------------------ */
/* SET / FIRE / no-data commands                                             */
/* ------------------------------------------------------------------------ */
static void test_set_fire(void) {
    printf("msc SET / FIRE\n");
    msc_msg_t d;
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x7F, 0x7F, 0x00, 0x40, 0xF7);
    CHECK(d.warn == 0 && d.ctrl == 16383 && d.value == 8192 && !d.time.present);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x00, 0x04, 0x7F, 0x01, 0x20, 0, 5, 0, 0, 0xF7); /* 512 = chan 1 */
    CHECK(d.warn == 0 && d.ctrl == 512 && d.value == 255 && d.time.present && d.time.seconds == 5);
    CHECK_STR(sum(&d, 26), "MSC SET 512=255 00:00:05");
    CHECK_STR(sum(&d, 21), "MSC LTG SET 512=255~");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x01, 0x02, 0x03, 0xF7);
    CHECK(d.warn == MSC_W_SHORT && !d.has_ctrl);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x01, 0x02, 0x03, 0x04, 0x00, 0xF7);
    CHECK(d.warn == MSC_W_SHORT && d.has_ctrl);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 1, 2, 3, 4, 0, 0, 0, 0, 0, 9, 0xF7);
    CHECK(d.warn == MSC_W_EXTRA);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x07, 0x05, 0xF7);
    CHECK(d.warn == 0 && d.has_macro && d.macro == 5);
    CHECK_STR(sum(&d, 21), "MSC LTG FIRE M5");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x07, 0xF7);
    CHECK(d.warn == MSC_W_SHORT && !d.has_macro);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x07, 0x05, 0x06, 0xF7);
    CHECK(d.warn == MSC_W_EXTRA);
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x0A, 0xF7);
    CHECK(d.warn == 0);
    CHECK_STR(sum(&d, 21), "MSC ALL RESET");
    DEC(d, 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x08, '1', 0xF7);
    CHECK(d.warn == MSC_W_EXTRA);
    CHECK_STR(sum(&d, 21), "MSC ALL ALLOFF!");
}

/* ------------------------------------------------------------------------ */
/* two-phase commit                                                          */
/* ------------------------------------------------------------------------ */
static void test_2pc(void) {
    printf("msc two-phase commit\n");
    msc_msg_t d;
    /* spec 6.9.1: STANDBY sound cue 109, seq 001, d1-d4 zero. Checksum by
     * hand: LE pairs 2010 + 0000 + 0001 + 0000 + 0000 + 3031 + 0039 = 507B,
     * + device 01 = 507C, & 7F7F = 507C -> cc = 7C 50. */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x20, 0x7C, 0x50, 0x01, 0x00, 0, 0, 0, 0, '1', '0', '9', 0xF7);
    CHECK(d.warn == 0 && d.is_2pc && d.chk_checked && d.chk_ok && !d.chk_alt);
    CHECK(d.chk_rx == 0x507C && d.chk_calc == 0x507C && d.seq == 1 && d.has_d);
    CHECK_STR(d.cue, "109");
    CHECK_STR(sum(&d, 21), "MSC SND STBY #1 Q109");
    /* corrupt one byte: checksum error */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x20, 0x7C, 0x50, 0x01, 0x00, 0, 0, 0, 0, '1', '0', '8', 0xF7);
    CHECK(d.warn == MSC_W_CHECKSUM && !d.chk_ok && d.chk_calc == 0x507B);
    CHECK_STR(sum(&d, 21), "MSC SND STBY #1 Q108!");
    /* same message with a big-endian-pair checksum is accepted (flagged BE):
     * BE pairs 1020+0000+0100+0000+0000+3130+3900 = 7B50, +01 = 7B51 */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x20, 0x51, 0x7B, 0x01, 0x00, 0, 0, 0, 0, '1', '0', '9', 0xF7);
    CHECK(d.warn == 0 && d.chk_ok && d.chk_alt);
    /* spec 6.9.2: ABORT seq 002 status 10 04 from the flys (fmt 22): "motor failure".
     * status 1004h -> s1 = 01, s2 = 08. Checksum: 2622 + 0000 + 0801 + 0002 = 2E25, + dev 05 = 2E2A. */
    DEC(d, 0xF0, 0x7F, 0x05, 0x02, 0x22, 0x26, 0x2A, 0x2E, 0x01, 0x08, 0x02, 0x00, 0xF7);
    CHECK(d.warn == 0 && d.chk_ok && d.has_status && d.status == 0x1004 && d.seq == 2);
    CHECK_STR(msc_status_text(d.cmd, d.fmt, d.status), "motor failure");
    CHECK_STR(sum(&d, 21), "MSC ABORT #2 1004h");
    /* CANCELLED terminated (80 0C), built */
    msc_build_t b;
    memset(&b, 0, sizeof b);
    b.dev = 0x01; b.fmt = 0x01; b.cmd = MSC_CANCELLED; b.seq = 7; b.status = 0x800C;
    uint8_t m[MSC_MAX_LEN];
    size_t n = msc_build(&b, m, sizeof m);
    CHECK(n == 13 && m[8] == 0x03 && m[9] == 0x40 && m[10] == 7 && m[11] == 0);
    CHECK(dec(m, n, &d) && d.warn == 0 && d.status == 0x800C);
    CHECK_STR(msc_status_text(d.cmd, d.fmt, d.status), "terminated");
    CHECK_STR(sum(&d, 21), "MSC CNCLD #7 800Ch");
    CHECK_STR(sum(&d, 26), "MSC LTG CNCLD #7 800Ch @1");
    /* status code tables */
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x8040), "deadman interlock not established");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x8028), "manual override initiated");
    CHECK_STR(msc_status_text(MSC_CANCELLED, 0x01, 0x8028), "manual override in progress");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x8030), "manual override in progress");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x10, 0x1008), "amplifier overload");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x100C), "strobe not charged");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x30, 0x1008), "time code lost");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x42, 0x1008), "lamp failure");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x5F, 0x1204), "burned out cue light");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x61, 0x1008), "atmospheric conditions prohibit discharge");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x7F, 0x1004), "format dependent (undefined)");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x0100), "manufacturer specific");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x0000), "unknown/undefined error");
    CHECK_STR(msc_status_text(MSC_ABORT, 0x01, 0x80B0), "reading new show cues from disk");
    CHECK_STR(msc_status_text(MSC_CANCELLED, 0x01, 0x8040), "undefined status");

    /* STANDING_BY with max execution time 00:00:30:00 */
    msc_time_t t = tc(MSC_TC_30, 0, 0, 30, 0, 0);
    memset(&b, 0, sizeof b);
    b.dev = 0x01; b.fmt = 0x24; b.cmd = MSC_STANDING_BY; b.seq = 4; b.time = &t;
    n = msc_build(&b, m, sizeof m);
    CHECK(dec(m, n, &d) && d.warn == 0 && d.time.seconds == 30 && !d.has_cue);
    CHECK_STR(sum(&d, 26), "MSC STBYNG #4 00:00:30:00");
    CHECK_STR(sum(&d, 21), "MSC TTBL STBYNG #4");
    /* COMPLETE seq only */
    memset(&b, 0, sizeof b);
    b.dev = 0x01; b.fmt = 0x10; b.cmd = MSC_COMPLETE; b.seq = 10;
    n = msc_build(&b, m, sizeof m);
    CHECK(n == 11 && dec(m, n, &d) && d.warn == 0 && d.seq == 10);
    CHECK_STR(sum(&d, 21), "MSC SND COMPL #10 @1");
    /* sequence number 0 is reserved */
    memset(&b, 0, sizeof b);
    b.dev = 0x01; b.fmt = 0x10; b.cmd = MSC_GO_2PC; b.cue = "1";
    n = msc_build(&b, m, sizeof m);
    CHECK(dec(m, n, &d) && d.warn == MSC_W_SEQ0);
    /* STANDBY / CANCEL require a cue */
    b.seq = 3; b.cue = NULL; b.cmd = MSC_CANCEL;
    n = msc_build(&b, m, sizeof m);
    CHECK(dec(m, n, &d) && d.warn == MSC_W_Q_MISSING);
    b.cmd = MSC_STANDBY;
    n = msc_build(&b, m, sizeof m);
    CHECK(dec(m, n, &d) && d.warn == MSC_W_Q_MISSING);
    /* short / long fixed parts */
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x20, 0x00, 0x00, 0x01, 0xF7);
    CHECK((d.warn & MSC_W_SHORT) && !d.has_seq);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x26, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xF7);
    CHECK(d.warn & MSC_W_EXTRA);
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x26, 0x00, 0xF7);
    CHECK((d.warn & MSC_W_SHORT) && !d.has_chk && !d.has_status);
}

/* ------------------------------------------------------------------------ */
/* capture limits, parser path, malformed / fuzz                             */
/* ------------------------------------------------------------------------ */
static void test_malformed(void) {
    printf("msc malformed / capture\n");
    msc_msg_t d;
    char t[MIDI_TEXT_MAX];
    decode_opts_t o = { false, 0 };

    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0xF7);
    CHECK(d.warn == MSC_W_SHORT && !d.has_fmt && !d.has_cmd);
    CHECK_STR(sum(&d, 21), "MSC ? @1!");
    DEC(d, 0xF0, 0x7F, 0x01, 0x02, 0x01, 0xF7);
    CHECK(d.warn == MSC_W_SHORT && d.has_fmt && !d.has_cmd);
    CHECK_STR(sum(&d, 21), "MSC LTG ? @1!");

    /* unterminated: ended by a status byte */
    const uint8_t u[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '5', 0x90, 0x3C, 0x40 };
    parse(u, sizeof u);
    CHECK(g_nmsg == 2);
    parse(u, 7 + 1);
    CHECK(g_last.type == MT_SYSEX && (g_last.flags & MSG_F_SX_UNTERM));
    CHECK(msc_decode_midi(&g_last, &d) && d.warn == MSC_W_UNTERM);
    midi_decode(&g_last, &o, t, sizeof t);
    CHECK_STR(t, "MSC LTG GO Q5 @1!");

    /* exactly 128 bytes: kept whole (MIDI_SX_STORE covers the MSC limit) */
    uint8_t m[200];
    size_t n = 0;
    m[n++] = 0xF0; m[n++] = 0x7F; m[n++] = 0x01; m[n++] = 0x02; m[n++] = 0x01; m[n++] = 0x01;
    while (n < 20) m[n++] = '1';
    m[n++] = 0x00;
    while (n < 127) m[n++] = '2';
    m[n++] = 0xF7;
    CHECK(n == 128 && MIDI_SX_STORE >= MSC_MAX_LEN);
    parse(m, n);
    CHECK(g_last.sx_len == 128 && !(g_last.flags & MSG_F_SX_TRUNC));
    CHECK(msc_decode_midi(&g_last, &d) && d.warn == MSC_W_Q_LONG && strlen(d.cue) == 14);
    /* 150 bytes: truncated by the capture, over the spec limit */
    n = 127;
    while (n < 149) m[n++] = '3';
    m[n++] = 0xF7;
    parse(m, n);
    CHECK(g_last.sx_len == 150 && (g_last.flags & MSG_F_SX_TRUNC));
    CHECK(msc_decode_midi(&g_last, &d) && (d.warn & MSC_W_TRUNC) && (d.warn & MSC_W_TOO_LONG) && !(d.warn & MSC_W_UNTERM));
    CHECK(d.len == 150);

    /* fuzz: random MSC-headed messages, every width, plus detail pages */
    uint32_t r = 0x1234567u;
    int fuzz_bad = 0;
    for (int it = 0; it < 30000; it++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        size_t len = 4 + r % 140u;
        m[0] = 0xF0; m[1] = 0x7F; m[2] = (uint8_t)(r >> 8) & 0x7F; m[3] = 0x02;
        for (size_t i = 4; i < len; i++) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            uint32_t k = r % 16u;
            m[i] = k < 4 ? 0x00 : k < 9 ? (uint8_t)('0' + r % 10u) : k < 10 ? '.' : (uint8_t)((r >> 9) & 0x7F);
        }
        if (len > 6 && (it & 1)) { m[4] = (uint8_t)(r >> 3) & 0x7F; m[5] = (uint8_t)(0x20 + (r >> 11) % 8u); }
        if (it % 3) m[len++] = 0xF7;
        size_t stored = len < MIDI_SX_STORE ? len : MIDI_SX_STORE;
        if (!msc_decode(m, stored, len, &d)) { fuzz_bad++; continue; }
        for (unsigned w = 1; w <= 26; w++) {
            msc_summary(&d, w, t, sizeof t);
            if (strlen(t) > w) { CHECK(strlen(t) <= w); break; }
        }
        parse(m, len);
        if (g_nmsg && g_last.type == MT_SYSEX) {
            detail(&g_last, 21);
            if (!lines_fit(21)) CHECK(lines_fit(21));
            detail(&g_last, 26);
            if (!lines_fit(26)) CHECK(lines_fit(26));
        }
    }
    CHECK(fuzz_bad == 0);   /* and survived ASan / UBSan */
}

/* ------------------------------------------------------------------------ */
/* generator round trip                                                      */
/* ------------------------------------------------------------------------ */
static void test_roundtrip(void) {
    printf("msc generator round trip\n");
    static const struct { uint16_t ci; uint8_t cd, list; } cues[] = {
        { 0, 0, 0 }, { 1, 0, 0 }, { 12, 5, 0 }, { 12, 5, 1 }, { 9999, 99, 127 }, { 0, 5, 3 }, { 0, 0, 4 },
    };
    static const uint8_t devs[] = { 0x00, 0x01, 0x6F, 0x70, 0x7E, 0x7F };
    int built = 0, bad = 0;
    for (unsigned fi = 0; fi < msc_gen_format_count(); fi++)
    for (unsigned ci = 0; ci < msc_gen_command_count(); ci++)
    for (size_t q = 0; q < sizeof cues / sizeof cues[0]; q++) {
        msc_gen_t g = {
            .dev = devs[(fi + q) % sizeof devs], .fmt = msc_gen_format(fi), .cmd = msc_gen_command(ci),
            .cue_int = cues[q].ci, .cue_dec = cues[q].cd, .list = cues[q].list,
            .time_s = (uint16_t)((fi * 97u + q * 13u) % 3600u), .ctrl = (uint16_t)((fi * 300u + q) % 16384u),
            .value = (uint16_t)(16383u - ci * 7u), .macro = (uint8_t)(fi + ci),
        };
        uint8_t m[64];    /* ME_GEN_MAX_LEN */
        size_t n = msc_gen_build(&g, m, sizeof m);
        char cue[12];
        msc_gen_cue_text(g.cue_int, g.cue_dec, cue, sizeof cue);
        if (g.cmd == MSC_LOAD && !cue[0]) { CHECK(n == 0); continue; }
        built++;
        msc_msg_t d;
        bool ok = n > 0 && dec(m, n, &d) && d.warn == 0 && d.dev == g.dev && d.fmt == g.fmt && d.cmd == g.cmd;
        bool cue_cmd = g.cmd == MSC_GO || g.cmd == MSC_STOP || g.cmd == MSC_RESUME || g.cmd == MSC_LOAD ||
                       g.cmd == MSC_GO_OFF || g.cmd == MSC_TIMED_GO;
        if (ok && cue_cmd) {
            ok = d.has_cue == (cue[0] != 0) && (!d.has_cue || strcmp(d.cue, cue) == 0);
            char l[6] = "";
            if (cue[0] && g.list) snprintf(l, sizeof l, "%u", g.list);
            ok = ok && d.has_list == (l[0] != 0) && (!d.has_list || strcmp(d.list, l) == 0) && !d.has_path;
        }
        if (ok && (g.cmd == MSC_TIMED_GO || (g.cmd == MSC_SET && g.time_s)))
            ok = d.time.present && d.time.type == MSC_TC_25 &&
                 d.time.hours * 3600u + d.time.minutes * 60u + d.time.seconds == g.time_s &&
                 d.time.frames == 0 && d.time.subframes == 0;
        if (ok && g.cmd == MSC_SET) ok = d.has_ctrl && d.ctrl == g.ctrl && d.value == g.value && (d.time.present == (g.time_s != 0));
        if (ok && g.cmd == MSC_FIRE) ok = d.has_macro && d.macro == g.macro;
        if (ok && (g.cmd == MSC_ALL_OFF || g.cmd == MSC_RESTORE || g.cmd == MSC_RESET)) ok = n == 7;
        /* and through the parser + monitor text */
        if (ok) {
            parse(m, n);
            char t[MIDI_TEXT_MAX];
            decode_opts_t o = { false, 0 };
            ok = g_nmsg == 1 && midi_decode(&g_last, &o, t, sizeof t) == CLS_SYSEX &&
                 strncmp(t, "MSC ", 4) == 0 && strlen(t) <= 21 && !strchr(t, '!');
        }
        if (!ok) { if (bad < 5) printf("  round trip fail fmt %02X cmd %02X q%zu\n", g.fmt, g.cmd, q); bad++; }
    }
    CHECK(bad == 0);
    CHECK(built > 3500);
    printf("  %d generated messages decoded back\n", built);

    /* specific generator outputs */
    msc_gen_t g = { .dev = 0x01, .fmt = 0x01, .cmd = MSC_GO, .cue_int = 12, .cue_dec = 5, .list = 1 };
    uint8_t m[64];
    size_t n = msc_gen_build(&g, m, sizeof m);
    const uint8_t exp[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0x00, '1', 0xF7 };
    CHECK(n == sizeof exp && memcmp(m, exp, n) == 0);
    g = (msc_gen_t){ .dev = 0x7F, .fmt = 0x10, .cmd = MSC_TIMED_GO, .cue_int = 4, .time_s = 3723 };
    n = msc_gen_build(&g, m, sizeof m);
    const uint8_t exp2[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x10, 0x04, 0x21, 0x02, 0x03, 0x00, 0x00, '4', 0xF7 };
    CHECK(n == sizeof exp2 && memcmp(m, exp2, n) == 0);
    g = (msc_gen_t){ .dev = 0x7F, .fmt = 0x01, .cmd = MSC_SET, .ctrl = 3, .value = 255 };
    n = msc_gen_build(&g, m, sizeof m);
    const uint8_t exp3[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x03, 0x00, 0x7F, 0x01, 0xF7 };
    CHECK(n == sizeof exp3 && memcmp(m, exp3, n) == 0);
    g = (msc_gen_t){ .dev = 0x7F, .fmt = 0x7F, .cmd = MSC_FIRE, .macro = 9 };
    n = msc_gen_build(&g, m, sizeof m);
    const uint8_t exp4[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x7F, 0x07, 0x09, 0xF7 };
    CHECK(n == sizeof exp4 && memcmp(m, exp4, n) == 0);
    CHECK(msc_build(&(msc_build_t){ .dev = 1, .fmt = 1, .cmd = 1, .cue = "12345" }, m, 8) == 0);   /* too small */
}

/* ------------------------------------------------------------------------ */
/* detail page                                                               */
/* ------------------------------------------------------------------------ */
static void test_detail(void) {
    printf("msc detail page\n");
    const uint8_t go[] = { 0xF0, 0x7F, 0x01, 0x02, 0x01, 0x01, '1', '2', '.', '5', 0x00, '1', 0xF7 };
    parse(go, sizeof go);
    unsigned n = detail(&g_last, 21);
    CHECK(n == (unsigned)d_n && lines_fit(21));
    CHECK(has_line("Dev  01h (1)"));
    CHECK(has_line("Fmt  01h LTG"));
    CHECK(has_line("  Lighting (General"));
    CHECK(has_line("  Category)"));
    CHECK(has_line("Cmd  01h GO"));
    CHECK(has_line("Cue  12.5"));
    CHECK(has_line("List 1"));
    CHECK(has_line("Raw 13 bytes"));
    CHECK(has_line("F0 7F 01 02 01 01 31"));
    CHECK(has_line("32 2E 35 00 31 F7"));
    if (getenv("MSC_SHOW")) {
        printf("  OLED monitor (paused on the MSC line) and detail pages:\n");
        static const char mon[][64] = {
            "NOTE ON  1 C4   100", "NOTE OFF 1 C4     0", "CC  1 #7=100", "PC  1 4",
            "MSC SND STOP", "MSC ALL GO Q1 L2", "MSC LTG GO Q12.5 L1"
        };
        show_screen("PAUSED +7           *", mon, 7, 21, 8);
        int pages = (d_n + 6) / 7;
        for (int p = 0; p < pages; p++) {
            char h[48];
            snprintf(h, sizeof h, "MSC 00:12.345     %d/%d", p + 1, pages);
            show_screen(h, (const char (*)[64])d_lines[p * 7], d_n - p * 7, 21, 8);
        }
    }
    detail(&g_last, 26);
    CHECK(lines_fit(26) && has_line("  Lighting (General") && has_line("F0 7F 01 02 01 01 31 32 2E"));

    /* SET with lighting control annotation */
    const uint8_t set[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x03, 0x00, 0x7F, 0x01, 0xF7 };
    parse(set, sizeof set);
    detail(&g_last, 21);
    CHECK(has_line("Ctrl 3") && has_line("  = sub master 4") && has_line("Val  255") && has_line("Dev  7Fh All-call"));
    const uint8_t gm[] = { 0xF0, 0x7F, 0x7F, 0x02, 0x01, 0x06, 0x7E, 0x03, 0x00, 0x00, 0xF7 };
    parse(gm, sizeof gm);
    detail(&g_last, 21);
    CHECK(has_line("Ctrl 510") && has_line("  = grand master"));

    /* TIMED_GO, 2PC ABORT, warnings */
    const uint8_t tg[] = { 0xF0, 0x7F, 0x70, 0x02, 0x10, 0x04, 0x41, 0x40, 0x03, 0x04, 0x05, '4', 0xF7 };
    parse(tg, sizeof tg);
    detail(&g_last, 21);
    CHECK(has_line("Dev  70h Group 1") && has_line("Cmd  04h TIMED_GO") && has_line("Time 01:00:03:04.05"));
    CHECK(has_line("Rate 30 drop-frame,") && has_line("     colour frame") && has_line("Cue  4"));
    const uint8_t ab[] = { 0xF0, 0x7F, 0x05, 0x02, 0x22, 0x26, 0x2A, 0x2E, 0x01, 0x08, 0x02, 0x00, 0xF7 };
    parse(ab, sizeof ab);
    detail(&g_last, 21);
    CHECK(has_line("Chk  2E2Ah ok") && has_line("Stat 10 04") && has_line("  motor failure") && has_line("Seq  2"));
    const uint8_t bad[] = { 0xF0, 0x7F, 0x01, 0x02, 0x10, 0x20, 0x7C, 0x50, 0x01, 0x00, 0, 0, 0, 0, '1', '0', '8', 0xF7 };
    parse(bad, sizeof bad);
    detail(&g_last, 21);
    CHECK(has_line("Chk  507Ch BAD") && has_line("Calc 507Bh") && has_line("! 2PC checksum bad"));
    CHECK(has_line("Data 00 00 00 00"));
    bool err_cls = false;
    for (int i = 0; i < d_n; i++) if (strcmp(d_lines[i], "! 2PC checksum bad") == 0) err_cls = d_cls[i] == CLS_ERROR;
    CHECK(err_cls);
    const uint8_t cc[] = { 0xF0, 0x7F, 0x01, 0x02, 0x1C, 0x1C, 0xF7 };
    parse(cc, sizeof cc);
    detail(&g_last, 21);
    CHECK(has_line("Cmd  1Ch") && has_line("  CLOSE_CUE_LIST") && has_line("List (none)") && has_line("! required Q missing"));

    /* any other sysex gets a raw detail page */
    const uint8_t sx[] = { 0xF0, 0x00, 0x20, 0x1F, 1, 2, 3, 4, 5, 6, 7, 0xF7 };
    parse(sx, sizeof sx);
    detail(&g_last, 21);
    CHECK(has_line("Len  12 bytes") && has_line("ID   00 20 1F") && has_line("Raw 12 bytes"));
    CHECK(has_line("F0 00 20 1F 01 02 03") && has_line("04 05 06 07 F7"));
    const uint8_t mmc[] = { 0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7 };
    parse(mmc, sizeof mmc);
    detail(&g_last, 21);
    CHECK(has_line("ID   7Fh universal RT") && has_line("Sub  06h 02h"));
    /* long sysex: hex limited to what is kept */
    uint8_t lg[300];
    lg[0] = 0xF0;
    for (int i = 1; i < 299; i++) lg[i] = (uint8_t)(i & 0x7F);
    lg[299] = 0xF7;
    parse(lg, sizeof lg);
    n = detail(&g_last, 21);
    CHECK(has_line("Raw 300b, 128 kept") && has_line("! capture truncated"));
    CHECK(n == 4u + (128u + 6u) / 7u && lines_fit(21));
}

void test_msc(void) {
    test_tables();
    test_basic();
    test_all_commands();
    test_cues();
    test_time();
    test_set_fire();
    test_2pc();
    test_malformed();
    test_roundtrip();
    test_detail();
}
