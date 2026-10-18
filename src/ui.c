/*
 * ui.c - monitor view and menus.
 *
 * Monitor (home):  UP/DOWN scroll history (pauses), LEFT back to live,
 *                  RIGHT toggles the time column, PRESS opens the menu, or
 *                  the detail page when paused on a sysex / MSC line.
 * Detail page:     UP/DOWN scroll, RIGHT/PRESS next page, LEFT back.
 * Menus:           UP/DOWN move, PRESS or RIGHT select / toggle / edit,
 *                  LEFT back. While editing a value: UP/DOWN change it
 *                  (auto-repeat), PRESS/LEFT/RIGHT finish.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

#include "ui.h"
#include "config.h"
#include "display.h"
#include "monitor.h"
#include "midi_decode.h"
#include "midi_io.h"
#include "midi_defs.h"
#include "generator.h"
#include "settings.h"
#include "msc_decode.h"

/* ------------------------------------------------------------------------ */
/* Menu model                                                                */
/* ------------------------------------------------------------------------ */
typedef enum { MI_SUB, MI_ACTION, MI_TOGGLE, MI_VALUE, MI_INFO, MI_CUE } mi_kind_t;

typedef struct menu menu_t;
typedef struct {
    const char *label;
    uint8_t kind;
    const menu_t *sub;
    void (*action)(void);
    int  (*get)(void);                 /* toggle / value / action state */
    void (*set)(int v);
    int16_t min, max, step;            /* MI_INFO: min is a parameter for fmt */
    void (*fmt)(int v, char *buf, size_t n);
} menu_item_t;

struct menu {
    const char *title;
    const menu_item_t *items;
    uint8_t count;
};

#define MENU(t, arr) { t, arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0])) }

/* ------------------------------------------------------------------------ */
/* UI state                                                                  */
/* ------------------------------------------------------------------------ */
typedef enum { SCR_MONITOR, SCR_MENU, SCR_DETAIL } screen_t;

#define MENU_DEPTH 4
static screen_t s_screen = SCR_MONITOR;
static const menu_t *s_stack[MENU_DEPTH];
static uint8_t s_sel[MENU_DEPTH], s_top[MENU_DEPTH];
static int s_depth = -1;
static bool s_editing;
static uint8_t s_cue_field;      /* MI_CUE edit: 0 integer part, 1 decimal part */
static bool s_need_render = true;

/* monitor */
static bool     s_paused;
static uint32_t s_anchor;        /* seq shown on the bottom row while paused */

static const char *s_disp_name = "?";

/* detail page (snapshot of one sysex / MSC message) */
#define DETAIL_MAX_LINES 96
static char     s_dl[DETAIL_MAX_LINES][CON_MAX_COLS + 1];
static uint8_t  s_dl_cls[DETAIL_MAX_LINES];
static unsigned s_dl_n, s_dl_top;
static midi_msg_t s_dl_msg;
static bool     s_dl_msc;

/* ------------------------------------------------------------------------ */
/* Getters / setters / formatters                                            */
/* ------------------------------------------------------------------------ */
/* Routing grid: one getter / setter pair per (output, source) cell. */
#define ROUTE_CELL(name, o, col) \
    static int  get_##name(void)  { return g_settings.route.src[o][col]; } \
    static void set_##name(int v) { g_settings.route.src[o][col] = v != 0; }
ROUTE_CELL(r_aa, ME_OUT_A, ME_SRC_A)
ROUTE_CELL(r_ab, ME_OUT_A, ME_SRC_B)
ROUTE_CELL(r_ag, ME_OUT_A, ME_SRC_GEN)
ROUTE_CELL(r_ba, ME_OUT_B, ME_SRC_A)
ROUTE_CELL(r_bb, ME_OUT_B, ME_SRC_B)
ROUTE_CELL(r_bg, ME_OUT_B, ME_SRC_GEN)
#undef ROUTE_CELL

#define ROUTE_PRESET(n) \
    static void act_preset##n(void) { route_preset_apply(&g_settings.route, n); } \
    static int  get_preset##n(void) { return route_preset_active(&g_settings.route, n); }
ROUTE_PRESET(0)
ROUTE_PRESET(1)
ROUTE_PRESET(2)
ROUTE_PRESET(3)
#undef ROUTE_PRESET
static void fmt_mark(int v, char *b, size_t n) { snprintf(b, n, "%s", v ? "*" : ""); }

static int  get_showin(void)  { return g_settings.show_inputs; }
static void set_showin(int v) { g_settings.show_inputs = (uint8_t)v; }
static void fmt_showin(int v, char *b, size_t n) {
    snprintf(b, n, "%s", v == SHOW_IN_A ? "A only" : v == SHOW_IN_B ? "B only" : "A+B");
}

static int  get_ch(void)      { return g_settings.gen_channel; }
static void set_ch(int v)     { g_settings.gen_channel = (uint8_t)v; }
static int  get_ccn(void)     { return g_settings.cc_number; }
static void set_ccn(int v)    { g_settings.cc_number = (uint8_t)v; }
static int  get_ccv(void)     { return g_settings.cc_value; }
static void set_ccv(int v)    { g_settings.cc_value = (uint8_t)v; }
static int  get_pc(void)      { return g_settings.program; }
static void set_pc(int v)     { g_settings.program = (uint8_t)v; }
static int  get_bpm(void)     { return g_settings.bpm; }
static void set_bpm(int v)    { g_settings.bpm = (uint16_t)v; gen_clock_bpm_changed(); }
static int  get_clock(void)   { return gen_clock_running(); }
static void set_clock(int v)  { if ((v != 0) != gen_clock_running()) gen_clock_toggle(); }
static int  get_nsweep(void)  { return gen_note_sweep_active(); }
static int  get_pbsweep(void) { return gen_pb_sweep_active(); }

static int  get_hide_rt(void) { return g_settings.hide_realtime; }
static void set_hide_rt(int v){ g_settings.hide_realtime = v != 0; }
static int  get_c3(void)      { return g_settings.note_c3; }
static void set_c3(int v)     { g_settings.note_c3 = v != 0; }
static int  get_timecol(void) { return g_settings.time_column; }
static void set_timecol(int v){ g_settings.time_column = v != 0; }
static int  get_usblog(void)  { return g_settings.usb_log; }
static void set_usblog(int v) { g_settings.usb_log = v != 0; }
static int  get_bright(void)  { return g_settings.brightness; }
static void set_bright(int v) {
    g_settings.brightness = (uint8_t)v;
    const display_driver_t *d = con_driver();
    if (d && d->set_brightness) d->set_brightness((uint8_t)v);
}

static int  get_msc_only(void) { return g_settings.msc_only; }
static void set_msc_only(int v){ g_settings.msc_only = v != 0; }
static int  get_devf(void)    { return g_settings.msc_dev_filter; }
static void set_devf(int v)   { g_settings.msc_dev_filter = (int16_t)v; }

static int  get_mcmd(void)    { return g_settings.msc_cmd_idx; }
static void set_mcmd(int v)   { g_settings.msc_cmd_idx = (uint8_t)v; }
static int  get_mfmt(void)    { return g_settings.msc_fmt_idx; }
static void set_mfmt(int v)   { g_settings.msc_fmt_idx = (uint8_t)v; }
static int  get_mdev(void)    { return g_settings.msc_dev; }
static void set_mdev(int v)   { g_settings.msc_dev = (uint8_t)v; }
static int  get_mlist(void)   { return g_settings.msc_list; }
static void set_mlist(int v)  { g_settings.msc_list = (uint8_t)v; }
static int  get_mtime(void)   { return g_settings.msc_time_s; }
static void set_mtime(int v)  { g_settings.msc_time_s = (uint16_t)v; }
static int  get_mctrl(void)   { return g_settings.msc_ctrl; }
static void set_mctrl(int v)  { g_settings.msc_ctrl = (uint16_t)v; }
static int  get_mval(void)    { return g_settings.msc_value; }
static void set_mval(int v)   { g_settings.msc_value = (uint16_t)v; }
static int  get_mmac(void)    { return g_settings.msc_macro; }
static void set_mmac(int v)   { g_settings.msc_macro = (uint8_t)v; }

static void fmt_mcmd(int v, char *b, size_t n) { snprintf(b, n, "%s", msc_command_name(msc_gen_command((unsigned)v))); }
static void fmt_mfmt(int v, char *b, size_t n) {
    uint8_t f = msc_gen_format((unsigned)v);
    snprintf(b, n, "%s %02X", msc_format_abbr(f), (unsigned)f);
}
static void fmt_mdev(int v, char *b, size_t n)  { msc_device_short((uint8_t)v, b, n); }
static void fmt_devf(int v, char *b, size_t n)  { if (v < 0) snprintf(b, n, "all"); else msc_device_short((uint8_t)v, b, n); }
static void fmt_none0(int v, char *b, size_t n) { if (v) snprintf(b, n, "%d", v); else snprintf(b, n, "none"); }
static void fmt_mss(int v, char *b, size_t n)   { snprintf(b, n, "%d:%02d", v / 60, v % 60); }

static void fmt_c3(int v, char *b, size_t n)   { snprintf(b, n, "%s", v ? "C3=60" : "C4=60"); }
static void fmt_pct(int v, char *b, size_t n)  { snprintf(b, n, "%d%%", v); }

/* actions */
static void act_monitor(void);
static void act_note_test(void)  { gen_note_test(); }
static void act_note_sweep(void) { gen_note_sweep_toggle(); }
static void act_cc(void)         { gen_send_cc(); }
static void act_pc(void)         { gen_send_pc(); }
static void act_pb(void)         { gen_pb_sweep_toggle(); }
static void act_start(void)      { gen_realtime(MIDI_START); }
static void act_stop(void)       { gen_realtime(MIDI_STOP); }
static void act_cont(void)       { gen_realtime(MIDI_CONTINUE); }
static void act_msc_send(void)   { gen_msc_send(); }
static void act_mmc_play(void)   { gen_mmc(0x02); }
static void act_mmc_stop(void)   { gen_mmc(0x01); }
static void act_panic(void)      { gen_panic(); }
static void act_clear(void)      { monitor_clear(); s_paused = false; }

/* stats */
static void fmt_count(int t, char *b, size_t n) { snprintf(b, n, "%lu", (unsigned long)monitor_type_count((midi_type_t)t)); }
static void fmt_stat(int which, char *b, size_t n) {
    const me_stats_t *st = &g_engine.stats;
    const me_out_t *oa = &g_engine.out[ME_OUT_A], *ob = &g_engine.out[ME_OUT_B];
    unsigned long v = 0;
    switch (which) {
    case 1: v = monitor_total_msgs(); break;
    case 5: v = oa->drops; break;
    case 6: v = ob->drops; break;
    case 7: v = st->gen_msgs; break;
    case 8: v = st->gen_rt; break;
    case 9: v = (unsigned long)oa->gen_shed + ob->gen_shed; break;
    case 10: v = st->stalls; break;
    case 11: v = monitor_stray_bytes(); break;
    case 12: v = (unsigned long)oa->rt_shed + ob->rt_shed; break;
    default: break;
    }
    snprintf(b, n, "%lu", v);
}
/* Per-input stats: which = input * 16 + field. */
#define IST(in, f) ((in) * 16 + (f))
static void fmt_istat(int which, char *b, size_t n) {
    int in = which / 16;
    const me_in_stats_t *st = &g_engine.in[in].st;
    unsigned long v = 0;
    switch (which % 16) {
    case 0: v = st->bytes_in; break;
    case 1: v = monitor_input_msgs(in); break;
    case 2: v = st->uart_errors; break;
    case 3: v = st->mon_overflow; break;
    case 4: v = (unsigned long)g_engine.out[ME_OUT_A].merge_drops[in] + g_engine.out[ME_OUT_B].merge_drops[in]; break;
    case 5: v = st->orphan_data; break;
    case 6: v = st->stalls; break;
    default: break;
    }
    snprintf(b, n, "%lu", v);
}
static void fmt_uptime(int unused, char *b, size_t n) {
    (void)unused;
    uint32_t s = (uint32_t)(time_us_64() / 1000000u);
    snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600u),
             (unsigned long)((s / 60u) % 60u), (unsigned long)(s % 60u));
}

/* about */
static void fmt_about(int which, char *b, size_t n) {
    switch (which) {
    case 0: snprintf(b, n, "%s", MIDIUM_MODEL); break;
    case 1: snprintf(b, n, "%s", MIDIUM_VERSION); break;
    case 2: snprintf(b, n, "%s", __DATE__); break;
    case 3: snprintf(b, n, "%s", __TIME__); break;
    case 4: snprintf(b, n, "%s", s_disp_name); break;
    case 5: snprintf(b, n, "%s", midi_io_watchdog_reset() ? "WATCHDOG" : "power-on"); break;
    default: b[0] = 0; break;
    }
}

/* ------------------------------------------------------------------------ */
/* Menu tables                                                               */
/* ------------------------------------------------------------------------ */
#define ACT(l, f)              { l, MI_ACTION, NULL, f, NULL, NULL, 0, 0, 0, NULL }
#define ACTS(l, f, g)          { l, MI_ACTION, NULL, f, g, NULL, 0, 0, 0, NULL }
#define ACTF(l, f, g, fm)      { l, MI_ACTION, NULL, f, g, NULL, 0, 0, 0, fm }
#define TOG(l, g, s)           { l, MI_TOGGLE, NULL, NULL, g, s, 0, 1, 1, NULL }
#define TOGF(l, g, s, f)       { l, MI_TOGGLE, NULL, NULL, g, s, 0, 1, 1, f }
#define VAL(l, g, s, lo, hi, st, f) { l, MI_VALUE, NULL, NULL, g, s, lo, hi, st, f }
#define SUB(l, m)              { l, MI_SUB, m, NULL, NULL, NULL, 0, 0, 0, NULL }
#define INFO(l, f, p)          { l, MI_INFO, NULL, NULL, NULL, NULL, p, 0, 0, f }
#define CUE(l)                 { l, MI_CUE, NULL, NULL, NULL, NULL, 0, 0, 0, NULL }

static const menu_item_t msc_items[] = {
    ACT("Send", act_msc_send),
    VAL("Command", get_mcmd, set_mcmd, 0, MSC_GEN_COMMAND_COUNT - 1, 1, fmt_mcmd),
    VAL("Format", get_mfmt, set_mfmt, 0, MSC_GEN_FORMAT_COUNT - 1, 1, fmt_mfmt),
    VAL("Device", get_mdev, set_mdev, 0, 127, 1, fmt_mdev),
    CUE("Cue"),
    VAL("Cue list", get_mlist, set_mlist, 0, 127, 1, fmt_none0),
    VAL("Time (TGO/SET)", get_mtime, set_mtime, 0, 3599, 1, fmt_mss),
    VAL("SET control", get_mctrl, set_mctrl, 0, 16383, 1, NULL),
    VAL("SET value", get_mval, set_mval, 0, 16383, 1, NULL),
    VAL("FIRE macro", get_mmac, set_mmac, 0, 127, 1, NULL),
};
static const menu_t msc_menu = MENU("MSC", msc_items);

static const menu_item_t gen_items[] = {
    VAL("Channel", get_ch, set_ch, 1, 16, 1, NULL),
    ACT("Note test C4", act_note_test),
    ACTS("Note sweep", act_note_sweep, get_nsweep),
    VAL("CC number", get_ccn, set_ccn, 0, 127, 1, NULL),
    VAL("CC value", get_ccv, set_ccv, 0, 127, 1, NULL),
    ACT("Send CC", act_cc),
    VAL("Program", get_pc, set_pc, 0, 127, 1, NULL),
    ACT("Send PC", act_pc),
    ACTS("PB sweep", act_pb, get_pbsweep),
    VAL("Clock BPM", get_bpm, set_bpm, 30, 300, 1, NULL),
    TOG("Clock out", get_clock, set_clock),
    ACT("MIDI Start", act_start),
    ACT("MIDI Stop", act_stop),
    ACT("MIDI Continue", act_cont),
    SUB("MSC", &msc_menu),
    ACT("MMC Play", act_mmc_play),
    ACT("MMC Stop", act_mmc_stop),
    ACT("PANIC notes off", act_panic),
};

static const menu_item_t grid_items[] = {
    TOG("OUT A <- IN A", get_r_aa, set_r_aa),
    TOG("OUT A <- IN B", get_r_ab, set_r_ab),
    TOG("OUT A <- Gen", get_r_ag, set_r_ag),
    TOG("OUT B <- IN A", get_r_ba, set_r_ba),
    TOG("OUT B <- IN B", get_r_bb, set_r_bb),
    TOG("OUT B <- Gen", get_r_bg, set_r_bg),
};
static const menu_t grid_menu = MENU("Custom routing", grid_items);

static const menu_item_t route_items[] = {
    ACTF("A->A, A->B (thru)", act_preset0, get_preset0, fmt_mark),
    ACTF("A+B merge -> both", act_preset1, get_preset1, fmt_mark),
    ACTF("A->A, B->B (split)", act_preset2, get_preset2, fmt_mark),
    ACTF("B->A, B->B", act_preset3, get_preset3, fmt_mark),
    SUB("Custom", &grid_menu),
};

static const menu_item_t disp_items[] = {
    TOG("Hide realtime", get_hide_rt, set_hide_rt),
    TOGF("Note names", get_c3, set_c3, fmt_c3),
    TOG("Time column", get_timecol, set_timecol),
    VAL("Brightness", get_bright, set_bright, 10, 100, 10, fmt_pct),
    TOG("USB log", get_usblog, set_usblog),
    VAL("Show inputs", get_showin, set_showin, 0, 2, 1, fmt_showin),
    TOG("Show MSC only", get_msc_only, set_msc_only),
    VAL("MSC dev filter", get_devf, set_devf, -1, 127, 1, fmt_devf),
    ACT("Clear monitor", act_clear),
};

static const menu_item_t stats_items[] = {
    INFO("Messages", fmt_stat, 1),
    INFO("IN A bytes", fmt_istat, IST(ME_IN_A, 0)),
    INFO("IN A msgs", fmt_istat, IST(ME_IN_A, 1)),
    INFO("IN A UART err", fmt_istat, IST(ME_IN_A, 2)),
    INFO("IN A mon ovf", fmt_istat, IST(ME_IN_A, 3)),
    INFO("IN A merge drop", fmt_istat, IST(ME_IN_A, 4)),
    INFO("IN A orphans", fmt_istat, IST(ME_IN_A, 5)),
    INFO("IN A stalls", fmt_istat, IST(ME_IN_A, 6)),
    INFO("IN B bytes", fmt_istat, IST(ME_IN_B, 0)),
    INFO("IN B msgs", fmt_istat, IST(ME_IN_B, 1)),
    INFO("IN B UART err", fmt_istat, IST(ME_IN_B, 2)),
    INFO("IN B mon ovf", fmt_istat, IST(ME_IN_B, 3)),
    INFO("IN B merge drop", fmt_istat, IST(ME_IN_B, 4)),
    INFO("IN B orphans", fmt_istat, IST(ME_IN_B, 5)),
    INFO("IN B stalls", fmt_istat, IST(ME_IN_B, 6)),
    INFO("Note On", fmt_count, MT_NOTE_ON),
    INFO("Note Off", fmt_count, MT_NOTE_OFF),
    INFO("CC", fmt_count, MT_CC),
    INFO("Program", fmt_count, MT_PROGRAM),
    INFO("Pitch Bend", fmt_count, MT_PITCH_BEND),
    INFO("Chan AT", fmt_count, MT_CHAN_AT),
    INFO("Poly AT", fmt_count, MT_POLY_AT),
    INFO("SysEx", fmt_count, MT_SYSEX),
    INFO("MTC QF", fmt_count, MT_MTC_QF),
    INFO("Song Pos", fmt_count, MT_SONG_POS),
    INFO("Song Sel", fmt_count, MT_SONG_SEL),
    INFO("Tune Req", fmt_count, MT_TUNE_REQ),
    INFO("Clock", fmt_count, MT_CLOCK),
    INFO("Start", fmt_count, MT_START),
    INFO("Continue", fmt_count, MT_CONTINUE),
    INFO("Stop", fmt_count, MT_STOP),
    INFO("Active Sense", fmt_count, MT_ACTIVE_SENSE),
    INFO("Reset", fmt_count, MT_RESET),
    INFO("Undefined", fmt_count, MT_UNDEFINED),
    INFO("Errors", fmt_count, MT_ERROR),
    INFO("Stray data", fmt_stat, 11),
    INFO("Drops out A", fmt_stat, 5),
    INFO("Drops out B", fmt_stat, 6),
    INFO("In RT shed", fmt_stat, 12),
    INFO("Gen msgs", fmt_stat, 7),
    INFO("Gen RT", fmt_stat, 8),
    INFO("Gen RT shed", fmt_stat, 9),
    INFO("Stalls", fmt_stat, 10),
    INFO("Uptime", fmt_uptime, 0),
    ACT("Clear msg counts", act_clear),
};

static const menu_item_t about_items[] = {
    INFO(MIDIUM_NAME, fmt_about, 0),
    INFO("Version", fmt_about, 1),
    INFO("Built", fmt_about, 2),
    INFO("", fmt_about, 3),
    INFO("Display", fmt_about, 4),
    INFO("Last reset", fmt_about, 5),
    INFO("Unpowered:", NULL, 0),
    INFO(" IN A -> OUT A", NULL, 0),
    INFO(" IN B not passed", NULL, 0),
};

static const menu_t gen_menu   = MENU("Generate", gen_items);
static const menu_t route_menu = MENU("Routing", route_items);
static const menu_t disp_menu  = MENU("Display", disp_items);
static const menu_t stats_menu = MENU("Stats", stats_items);
static const menu_t about_menu = MENU("About", about_items);

static const menu_item_t main_items[] = {
    ACT("Monitor", act_monitor),
    SUB("Generate", &gen_menu),
    SUB("Routing", &route_menu),
    SUB("Display", &disp_menu),
    SUB("Stats", &stats_menu),
    SUB("About", &about_menu),
};
static const menu_t main_menu = MENU("Menu", main_items);

/* ------------------------------------------------------------------------ */
/* Navigation                                                                */
/* ------------------------------------------------------------------------ */
static void push_menu(const menu_t *m) {
    if (s_depth + 1 >= MENU_DEPTH) return;
    s_depth++;
    s_stack[s_depth] = m;
    s_sel[s_depth] = 0;
    s_top[s_depth] = 0;
    s_screen = SCR_MENU;
}

static void pop_menu(void) {
    s_editing = false;
    if (s_depth > 0) s_depth--;
    else { s_depth = -1; s_screen = SCR_MONITOR; }
}

static void act_monitor(void) {
    s_depth = -1;
    s_editing = false;
    s_screen = SCR_MONITOR;
}

static int menu_rows(void) { return con_rows() - 2; }   /* title + footer */

static void dl_line_cb(const char *t, msg_class_t cls, void *ctx) {
    (void)ctx;
    if (s_dl_n >= DETAIL_MAX_LINES) return;
    snprintf(s_dl[s_dl_n], sizeof s_dl[0], "%s", t);
    s_dl_cls[s_dl_n] = (uint8_t)cls;
    s_dl_n++;
}

static void open_detail(const midi_msg_t *m) {
    s_dl_msg = *m;
    s_dl_n = 0;
    s_dl_top = 0;
    unsigned have = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
    s_dl_msc = msc_is_msc(m->sx, have);
    dl_line_cb(m->src ? "In   IN B" : "In   IN A", CLS_SYSEX, NULL);
    sysex_detail(&s_dl_msg, (unsigned)con_cols(), dl_line_cb, NULL);
    s_screen = SCR_DETAIL;
}

static void menu_event(joy_event_t e) {
    const menu_t *m = s_stack[s_depth];
    const menu_item_t *it = &m->items[s_sel[s_depth]];

    if (s_editing && it->kind == MI_CUE) {
        int v = s_cue_field ? g_settings.msc_cue_dec : g_settings.msc_cue_int;
        int hi = s_cue_field ? 99 : 9999;
        switch (e) {
        case JOY_UP:    if (v < hi) v++; break;
        case JOY_DOWN:  if (v > 0) v--; break;
        case JOY_RIGHT: if (s_cue_field == 0) { s_cue_field = 1; return; } s_editing = false; return;
        case JOY_LEFT:  if (s_cue_field == 1) { s_cue_field = 0; return; } s_editing = false; return;
        default:        s_editing = false; return;
        }
        if (s_cue_field) g_settings.msc_cue_dec = (uint8_t)v;
        else g_settings.msc_cue_int = (uint16_t)v;
        return;
    }
    if (s_editing) {
        int v = it->get();
        switch (e) {
        case JOY_UP:   v += it->step; if (v > it->max) v = it->max; it->set(v); break;
        case JOY_DOWN: v -= it->step; if (v < it->min) v = it->min; it->set(v); break;
        default:       s_editing = false; break;
        }
        return;
    }

    switch (e) {
    case JOY_UP:
        if (s_sel[s_depth] > 0) s_sel[s_depth]--;
        else s_sel[s_depth] = (uint8_t)(m->count - 1);       /* wrap */
        break;
    case JOY_DOWN:
        if (s_sel[s_depth] + 1 < m->count) s_sel[s_depth]++;
        else s_sel[s_depth] = 0;
        break;
    case JOY_LEFT:
        pop_menu();
        return;
    case JOY_RIGHT:
    case JOY_PRESS:
        switch (it->kind) {
        case MI_SUB:    push_menu(it->sub); return;
        case MI_ACTION: if (it->action) it->action(); break;
        case MI_TOGGLE: it->set(!it->get()); break;
        case MI_VALUE:  s_editing = true; break;
        case MI_CUE:    s_editing = true; s_cue_field = 0; break;
        default: break;
        }
        break;
    default:
        break;
    }
    /* keep the selection visible */
    int rows = menu_rows();
    if (s_sel[s_depth] < s_top[s_depth]) s_top[s_depth] = s_sel[s_depth];
    if (s_sel[s_depth] >= s_top[s_depth] + rows) s_top[s_depth] = (uint8_t)(s_sel[s_depth] - rows + 1);
}

static int monitor_rows(void) { return con_rows() - 1; }

static void monitor_event(joy_event_t e) {
    uint32_t next = monitor_next_seq();
    uint32_t oldest = monitor_oldest_seq();
    if (next == 0 && e != JOY_PRESS && e != JOY_RIGHT) return;
    uint32_t min_anchor = oldest + (uint32_t)monitor_rows() - 1u;
    if (min_anchor > next - 1u) min_anchor = next - 1u;

    switch (e) {
    case JOY_UP:
        if (!s_paused) { s_paused = true; s_anchor = next - 1u; }
        if (s_anchor > min_anchor) s_anchor--;
        break;
    case JOY_DOWN:
        if (s_paused && s_anchor + 1u < next) s_anchor++;
        break;
    case JOY_LEFT:
        s_paused = false;
        break;
    case JOY_RIGHT:
        g_settings.time_column = !g_settings.time_column;
        break;
    case JOY_PRESS: {
        midi_msg_t m;
        if (s_paused && monitor_get(s_anchor, &m) && m.type == MT_SYSEX) open_detail(&m);
        else push_menu(&main_menu);
        break;
    }
    default:
        break;
    }
}

static int detail_rows(void) { return con_rows() - 1; }

static void detail_event(joy_event_t e) {
    unsigned rows = (unsigned)detail_rows();
    unsigned max_top = s_dl_n > rows ? s_dl_n - rows : 0u;
    switch (e) {
    case JOY_UP:    if (s_dl_top > 0) s_dl_top--; break;
    case JOY_DOWN:  if (s_dl_top < max_top) s_dl_top++; break;
    case JOY_RIGHT:
    case JOY_PRESS:                                   /* next page, wraps to the top */
        if (s_dl_top >= max_top) s_dl_top = 0;
        else s_dl_top = s_dl_top + rows > max_top ? max_top : s_dl_top + rows;
        break;
    case JOY_LEFT:  s_screen = SCR_MONITOR; break;
    default: break;
    }
}

void ui_event(joy_event_t e) {
    if (e == JOY_NONE) return;
    if (s_screen == SCR_MONITOR) monitor_event(e);
    else if (s_screen == SCR_DETAIL) detail_event(e);
    else menu_event(e);
    s_need_render = true;
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------ */
static void render_monitor(uint64_t now) {
    char hdr[CON_MAX_COLS + 1];
    int cols = con_cols();
    bool act = (uint32_t)(now / 1000u) - midi_io_last_rx_ms() < 150u && midi_io_last_rx_ms() != 0;
    char ra[4], rb[4];
    route_summary(&g_settings.route, ME_OUT_A, ra, sizeof ra);
    route_summary(&g_settings.route, ME_OUT_B, rb, sizeof rb);
    snprintf(hdr, sizeof hdr, "%-7s A:%-2s B:%s",
             s_paused ? "PAUSED" : MIDIUM_NAME, ra, rb);
    con_row(0, CLS_HEADER | ATTR_INVERSE, hdr);
    con_print(0, cols - 1, CLS_HEADER | ATTR_INVERSE, act ? "*" : " ");
    if (cols > 22 && gen_clock_running()) con_print(0, cols - 3, CLS_HEADER | ATTR_INVERSE, "C");

    uint32_t next = monitor_next_seq();
    uint32_t oldest = monitor_oldest_seq();
    if (s_paused) {
        if (s_anchor < oldest) s_anchor = oldest;           /* history overwritten */
        if (next && s_anchor >= next) s_anchor = next - 1u;
    }
    uint32_t bottom = s_paused ? s_anchor : next - 1u;
    int rows = monitor_rows();
    /* Each line starts with its input tag ("A " / "B "). */
    int width = con_cols() - 2 - (g_settings.time_column ? 10 : 0);
    decode_opts_t o = { .note_c3 = g_settings.note_c3, .width = (uint8_t)(width > 0 ? width : 1) };

    for (int r = 0; r < rows; r++) {
        int screen_row = con_rows() - 1 - r;
        midi_msg_t m;
        if (next == 0 || bottom < (uint32_t)r || !monitor_get(bottom - (uint32_t)r, &m)) {
            con_row(screen_row, CLS_DIM, r == 0 && next == 0 ? "  waiting for MIDI..." : "");
            continue;
        }
        char txt[MIDI_TEXT_MAX];
        char line[CON_MAX_COLS + 20];
        msg_class_t cls = midi_decode(&m, &o, txt, sizeof txt);
        char tag = m.src ? 'B' : 'A';
        if (g_settings.time_column) {
            char t[16];
            midi_format_time(m.t_us, t, sizeof t);
            snprintf(line, sizeof line, "%c %s %s", tag, t, txt);
        } else {
            snprintf(line, sizeof line, "%c %s", tag, txt);
        }
        uint8_t attr = (uint8_t)cls;
        if (s_paused && r == 0) attr |= ATTR_INVERSE;
        con_row(screen_row, attr, line);
    }
}

static void item_value_text(const menu_item_t *it, char *b, size_t n) {
    b[0] = 0;
    switch (it->kind) {
    case MI_SUB:
        snprintf(b, n, ">");
        break;
    case MI_TOGGLE:
        if (it->fmt) it->fmt(it->get(), b, n);
        else snprintf(b, n, "%s", it->get() ? "on" : "off");
        break;
    case MI_VALUE: {
        char v[12];
        if (it->fmt) it->fmt(it->get(), v, sizeof v);
        else snprintf(v, sizeof v, "%d", it->get());
        if (s_editing && it == &s_stack[s_depth]->items[s_sel[s_depth]]) snprintf(b, n, "<%s>", v);
        else snprintf(b, n, "%s", v);
        break;
    }
    case MI_ACTION:
        if (it->fmt) it->fmt(it->get ? it->get() : 0, b, n);
        else if (it->get && it->get()) snprintf(b, n, "RUN");
        break;
    case MI_INFO:
        if (it->fmt) it->fmt(it->min, b, n);
        break;
    case MI_CUE: {
        unsigned ci = g_settings.msc_cue_int, cd = g_settings.msc_cue_dec;
        if (s_editing && it == &s_stack[s_depth]->items[s_sel[s_depth]]) {
            if (s_cue_field == 0) snprintf(b, n, "<%u>.%u", ci, cd);
            else snprintf(b, n, "%u.<%u>", ci, cd);
        } else if (ci == 0 && cd == 0) {
            snprintf(b, n, "none");
        } else {
            msc_gen_cue_text((uint16_t)ci, (uint8_t)cd, b, n);
        }
        break;
    }
    default:
        break;
    }
}

static void render_menu(uint64_t now) {
    const menu_t *m = s_stack[s_depth];
    int rows = menu_rows();
    char title[CON_MAX_COLS + 1];
    if (m->count > rows) snprintf(title, sizeof title, "%s %u/%u", m->title, s_sel[s_depth] + 1u, (unsigned)m->count);
    else snprintf(title, sizeof title, "%s", m->title);
    con_row(0, CLS_HEADER | ATTR_INVERSE, title);

    for (int r = 0; r < rows; r++) {
        int idx = s_top[s_depth] + r;
        if (idx >= m->count) { con_row(1 + r, 0, ""); continue; }
        const menu_item_t *it = &m->items[idx];
        char val[CON_MAX_COLS + 1];
        item_value_text(it, val, sizeof val);
        uint8_t attr = (idx == s_sel[s_depth]) ? (CLS_NORMAL | ATTR_INVERSE) : CLS_NORMAL;
        if (it->kind == MI_INFO && idx != s_sel[s_depth]) attr = CLS_NORMAL;
        con_row_lr(1 + r, attr, it->label, val);
    }

    /* footer: recent generator output, else hints */
    const char *foot;
    uint8_t fattr = CLS_DIM;
    const menu_item_t *sel = &m->items[s_sel[s_depth]];
    if (s_editing && sel->kind == MI_CUE) foot = s_cue_field ? "UP/DN adj  LEFT int" : "UP/DN adj  RIGHT .dec";
    else if (s_editing) foot = "UP/DN adjust, PRESS ok";
    else if (gen_last_text()[0] && now - gen_last_time_us() < 3000000u) { foot = gen_last_text(); fattr = CLS_PC; }
    else foot = "PRESS select  LEFT back";
    con_row(con_rows() - 1, fattr, foot);
}

static void render_detail(void) {
    char left[CON_MAX_COLS + 1], right[12], t[16];
    int rows = detail_rows();
    unsigned pages = s_dl_n ? (s_dl_n + (unsigned)rows - 1u) / (unsigned)rows : 1u;
    unsigned page = s_dl_top / (unsigned)rows + 1u;
    if (s_dl_top + (unsigned)rows >= s_dl_n) page = pages;       /* last screen */
    midi_format_time(s_dl_msg.t_us, t, sizeof t);
    snprintf(left, sizeof left, "%s %c %s", s_dl_msc ? "MSC" : "SYX", s_dl_msg.src ? 'B' : 'A', t);
    snprintf(right, sizeof right, "%u/%u", page, pages);
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, left, right);
    for (int r = 0; r < rows; r++) {
        unsigned i = s_dl_top + (unsigned)r;
        if (i < s_dl_n) con_row(1 + r, s_dl_cls[i], s_dl[i]);
        else con_row(1 + r, CLS_NORMAL, "");
    }
}

void ui_render(uint64_t now, bool history_changed) {
    static uint64_t last;
    if (!s_need_render && !history_changed && now - last < 100000u) return;
    last = now;
    s_need_render = false;
    if (s_screen == SCR_MONITOR) render_monitor(now);
    else if (s_screen == SCR_DETAIL) render_detail();
    else render_menu(now);
}

void ui_init(void) {
    const display_driver_t *d = con_driver();
    s_disp_name = d ? d->name : "none";
    if (d && d->set_brightness) d->set_brightness(g_settings.brightness);
    s_screen = SCR_MONITOR;
    s_need_render = true;
}
