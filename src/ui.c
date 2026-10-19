/*
 * ui.c - monitor view, menus and the full-screen pages.
 *
 * Monitor (home):  the newest message is pinned to the bottom row (Display >
 *                  Pin latest). UP/DOWN scroll history (pauses) with a
 *                  cursor that can also land on the pinned line, LEFT back to
 *                  live, RIGHT cycles the time column, PRESS opens the menu,
 *                  or the detail page when paused on a sysex / MSC line.
 * Detail page:     UP/DOWN scroll, RIGHT/PRESS next page, LEFT back.
 * Menus:           UP/DOWN move (wraps), PRESS or RIGHT select / toggle /
 *                  edit, LEFT back. While editing a value: UP/DOWN change it
 *                  (wraps at both ends; auto-repeat, faster after ~1.3 s on
 *                  big ranges), PRESS/LEFT/RIGHT finish.
 * MTC display:     big hh:mm:ss:ff; LEFT back.
 * Date & time:     PRESS edit; LEFT/RIGHT field, UP/DOWN change, PRESS set.
 * Logs:            sessions newest first; PRESS browse; LEFT back.
 * Log browser:     as the monitor without pinning; LEFT back to Logs.
 * Power off:       Cancel / Power off (Cancel preselected).
 *
 * Over any screen: holding PRESS shows the power-off countdown (release to
 * cancel; JOY_PRESS only ever comes from a short tap, see joystick.h), the
 * auto-off warning (any key cancels it and does nothing else), the one-time
 * low-battery popup (any key closes it), and the shutdown / off screens.
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
#include "msg_filter.h"
#include "valedit.h"
#include "mtc.h"
#include "bigdigits.h"
#include "clock.h"
#include "rv3028.h"
#include "logger.h"
#include "timeutil.h"
#include "power.h"
#include "power_fsm.h"

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
typedef enum { SCR_MONITOR, SCR_MENU, SCR_DETAIL, SCR_MTC, SCR_DATETIME, SCR_LOGS, SCR_LOGDEL, SCR_BROWSE, SCR_PWRCONF } screen_t;

#define MENU_DEPTH 4
static screen_t s_screen = SCR_MONITOR;
static const menu_t *s_stack[MENU_DEPTH];
static uint8_t s_sel[MENU_DEPTH], s_top[MENU_DEPTH];
static int s_depth = -1;
static bool s_editing;
static uint8_t s_cue_field;      /* MI_CUE edit: 0 integer part, 1 decimal part */
static bool s_need_render = true;

/* held-key acceleration */
static joy_event_t s_rep_e;
static uint64_t s_rep_t;
static unsigned s_rep_n;

/* monitor */
static bool     s_paused;
static uint32_t s_anchor;        /* paused: seq on the bottom history row */
static uint32_t s_cursor;        /* paused, pinned mode: selected seq */
static bool     s_cur_pin;       /* paused, pinned mode: cursor on the pinned (newest) line */
static bool     s_anchor_none;   /* pinned mode: no history rows when paused */

static const char *s_disp_name = "?";

/* detail page (snapshot of one sysex / MSC message) */
#define DETAIL_MAX_LINES 96
static char     s_dl[DETAIL_MAX_LINES][CON_MAX_COLS + 1];
static uint8_t  s_dl_cls[DETAIL_MAX_LINES];
static unsigned s_dl_n, s_dl_top;
static midi_msg_t s_dl_msg;
static log_time_t s_dl_time;
static bool     s_dl_msc;
static screen_t s_dl_ret = SCR_MONITOR;

/* MTC page */
static char     s_big_last[16];
static uint8_t  s_big_cls;
static bool     s_big_force;

/* date & time */
static bool          s_dt_edit;
static uint8_t       s_dt_field;
static tu_datetime_t s_dt;

/* logs */
static int      s_lg_sel, s_lg_top;
static uint8_t  s_del_sel;
static uint32_t s_br_no;
static log_pos_t s_br_pos;
static bool     s_br_have;

/* power: overlays on top of the current screen */
typedef enum { OV_NONE = 0, OV_POWER, OV_HOLD, OV_AUTOOFF, OV_LOWBATT } overlay_t;
static overlay_t s_ov;               /* overlay drawn last */
static bool      s_hold;             /* power-off countdown running */
static uint64_t  s_hold_t;
static bool      s_lowbatt_popup;
static uint8_t   s_pwr_sel;          /* Power off confirm: 0 Cancel, 1 Power off */

/* short feedback line */
static char     s_toast[CON_MAX_COLS + 1];
static uint64_t s_toast_t;

/* Copy with truncation to the screen width (no format-truncation noise). */
static void copy_trunc(char *dst, size_t n, const char *src) {
    size_t k = strlen(src);
    if (k >= n) k = n - 1;
    memcpy(dst, src, k);
    dst[k] = 0;
}

static void toast(const char *t) {
    copy_trunc(s_toast, sizeof s_toast, t);
    s_toast_t = time_us_64();
}
static bool toast_live(uint64_t now) { return s_toast[0] && now - s_toast_t < 2500000u; }

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

/* Filters: presets and one toggle per category. */
#define FPRESET(n) \
    static void act_fpre##n(void) { msg_filter_apply_preset(&g_settings.filter, (filter_preset_t)n); } \
    static int  get_fpre##n(void) { return msg_filter_preset_active(&g_settings.filter, (filter_preset_t)n); }
FPRESET(0)
FPRESET(1)
FPRESET(2)
FPRESET(3)
#undef FPRESET
#define FCAT(c) \
    static int  get_fc_##c(void)  { return (g_settings.filter.types & FC_BIT(c)) != 0; } \
    static void set_fc_##c(int v) { if (v) g_settings.filter.types |= FC_BIT(c); else g_settings.filter.types &= ~FC_BIT(c); }
FCAT(FC_NOTE) FCAT(FC_POLY_AT) FCAT(FC_CC) FCAT(FC_PROGRAM) FCAT(FC_CHAN_AT) FCAT(FC_PITCH_BEND)
FCAT(FC_SYSEX) FCAT(FC_MSC) FCAT(FC_MMC) FCAT(FC_MTC_QF) FCAT(FC_MTC_FULL) FCAT(FC_SONG)
FCAT(FC_TUNE) FCAT(FC_CLOCK) FCAT(FC_TRANSPORT) FCAT(FC_ACTIVE_SENSE) FCAT(FC_RESET)
FCAT(FC_UNDEFINED) FCAT(FC_ERROR)
#undef FCAT

static int  get_showin(void)  { return g_settings.filter.inputs; }
static void set_showin(int v) { g_settings.filter.inputs = (uint8_t)v; }
static void fmt_showin(int v, char *b, size_t n) {
    snprintf(b, n, "%s", v == SHOW_IN_A ? "A only" : v == SHOW_IN_B ? "B only" : "A+B");
}
static int  get_fch(void)     { return g_settings.filter.channel; }
static void set_fch(int v)    { g_settings.filter.channel = (int8_t)v; }
static void fmt_fch(int v, char *b, size_t n) { if (v < 0) snprintf(b, n, "all"); else snprintf(b, n, "%d", v + 1); }
static int  get_devf(void)    { return g_settings.filter.msc_dev; }
static void set_devf(int v)   { g_settings.filter.msc_dev = (int16_t)v; }

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

static int  get_pin(void)     { return g_settings.pin_latest; }
static void set_pin(int v)    { g_settings.pin_latest = v != 0; s_paused = false; }
static int  get_c3(void)      { return g_settings.note_c3; }
static void set_c3(int v)     { g_settings.note_c3 = v != 0; }
static int  get_timecol(void) { return g_settings.time_col; }
static void set_timecol(int v){ g_settings.time_col = (uint8_t)v; }
static void fmt_timecol(int v, char *b, size_t n) {
    snprintf(b, n, "%s", v == TIMECOL_UPTIME ? "Uptime" : v == TIMECOL_TOD ? "Time of day" : "Off");
}
static int  get_usblog(void)  { return g_settings.usb_log; }
static void set_usblog(int v) { g_settings.usb_log = v != 0; }
static int  get_bright(void)  { return g_settings.brightness; }
static void set_bright(int v) {
    g_settings.brightness = (uint8_t)v;
    const display_driver_t *d = con_driver();
    if (d && d->set_brightness) d->set_brightness((uint8_t)v);
}

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

static void open_mtc(void);
static void open_datetime(void);
static void open_logs(void);
static void open_pwrconf(void);

/* stats */
static void fmt_count(int t, char *b, size_t n) { snprintf(b, n, "%lu", (unsigned long)monitor_type_count((midi_type_t)t)); }
static void fmt_stat(int which, char *b, size_t n) {
    const me_stats_t *st = &g_engine.stats;
    const me_out_t *oa = &g_engine.out[ME_OUT_A], *ob = &g_engine.out[ME_OUT_B];
    const log_t *L = logger_log();
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
    case 20:
        if (!logger_enabled()) { snprintf(b, n, "off"); return; }
        snprintf(b, n, "%u%%", log_used_pct(L));
        return;
    case 21: v = (unsigned long)log_session_count(L); break;
    case 22: v = L->drops; break;
    case 23: v = L->bad_pages; break;
    case 24: v = L->pages_written; break;
    case 25: v = L->erases; break;
    case 26: {
        const log_sess_t *e = L->live ? log_find(L, L->live_no) : NULL;
        if (!e) { snprintf(b, n, "-"); return; }
        snprintf(b, n, "#%lu %lu", (unsigned long)e->no, (unsigned long)e->nrec);
        return;
    }
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
    tu_fmt_duration((uint32_t)(time_us_64() / 1000000u), b, n);
}

/* power settings */
static int  get_usbpo(void)   { return g_settings.usb_power_on; }
static void set_usbpo(int v)  { g_settings.usb_power_on = (uint8_t)v; }
static void fmt_usbpo(int v, char *b, size_t n) {
    bool wide = con_cols() >= 26;
    if (v == USBPO_POWER_ON) snprintf(b, n, "%s", wide ? "Power on" : "on");
    else snprintf(b, n, "%s", wide ? "Charge only" : "charge");
}
static int  get_aoff(void)    { return g_settings.auto_off_idx; }
static void set_aoff(int v)   { g_settings.auto_off_idx = (uint8_t)v; power_activity(); }
static void fmt_aoff(int v, char *b, size_t n) {
    unsigned m = autooff_minutes((unsigned)v);
    if (!m) snprintf(b, n, "Off");
    else snprintf(b, n, con_cols() >= 26 ? "%u min" : "%um", m);
}

/* power / battery stats */
static void fmt_power(int which, char *b, size_t n) {
    const batt_info_t *bi = power_batt();
    switch (which) {
    case 0:                                       /* charge */
        if (!bi->present) snprintf(b, n, "none");
        else snprintf(b, n, "%u.%u%%", bi->soc_x10 / 10u, bi->soc_x10 % 10u);
        break;
    case 1:                                       /* cell voltage */
        if (!bi->present) snprintf(b, n, "-");
        else snprintf(b, n, "%u.%02u V", bi->mv / 1000u, (bi->mv % 1000u) / 10u);
        break;
    case 2:                                       /* supply */
        if (bi->usb) snprintf(b, n, "%s", !bi->present ? "USB" : bi->charging ? "USB, charging" : "USB, full");
        else snprintf(b, n, "%s", bi->present ? "battery" : "-");
        break;
    case 3: {                                     /* charge rate */
        if (!bi->present) { snprintf(b, n, "-"); break; }
        int32_t r = bi->crate_x1000 / 100;        /* 0.1 %/h */
        unsigned a = (unsigned)(r < 0 ? -r : r);
        snprintf(b, n, "%c%u.%u%%/h", r < 0 ? '-' : '+', a / 10u, a % 10u);
        break;
    }
    case 4: {                                     /* auto-off countdown */
        unsigned m = power_autooff_minutes();
        if (!m) snprintf(b, n, "off");
        else if (!bi->present || bi->usb) snprintf(b, n, "on USB");
        else tu_fmt_duration(power_autooff_secs_left(), b, n);
        break;
    }
    case 5: snprintf(b, n, "%s", power_last_off_text()); break;
    case 6: if (bi->present) snprintf(b, n, "MAX17048"); else snprintf(b, n, "none"); break;
    default: b[0] = 0; break;
    }
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
    case 6: snprintf(b, n, "%s", clock_rtc_present() ? "RV-3028" : "none"); break;
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

#define FTOG(c) TOG(NULL, get_fc_##c, set_fc_##c)
static const menu_item_t filter_items[] = {
    ACTF("All", act_fpre0, get_fpre0, fmt_mark),
    ACTF("Show control only", act_fpre1, get_fpre1, fmt_mark),
    ACTF("No realtime/TC", act_fpre2, get_fpre2, fmt_mark),
    ACTF("None", act_fpre3, get_fpre3, fmt_mark),
    FTOG(FC_NOTE), FTOG(FC_POLY_AT), FTOG(FC_CC), FTOG(FC_PROGRAM), FTOG(FC_CHAN_AT),
    FTOG(FC_PITCH_BEND), FTOG(FC_SYSEX), FTOG(FC_MSC), FTOG(FC_MMC), FTOG(FC_MTC_QF),
    FTOG(FC_MTC_FULL), FTOG(FC_SONG), FTOG(FC_TUNE), FTOG(FC_CLOCK), FTOG(FC_TRANSPORT),
    FTOG(FC_ACTIVE_SENSE), FTOG(FC_RESET), FTOG(FC_UNDEFINED), FTOG(FC_ERROR),
    VAL("Channel", get_fch, set_fch, -1, 15, 1, fmt_fch),
    VAL("Show inputs", get_showin, set_showin, 0, 2, 1, fmt_showin),
    VAL("MSC dev filter", get_devf, set_devf, -1, 127, 1, fmt_devf),
};
#undef FTOG
#define FILTER_FIRST_CAT 4          /* index of the first per-type toggle */
static const menu_t filter_menu = MENU("Filters", filter_items);

static const menu_item_t disp_items[] = {
    TOG("Pin latest", get_pin, set_pin),
    SUB("Filters", &filter_menu),
    TOGF("Note names", get_c3, set_c3, fmt_c3),
    VAL("Time column", get_timecol, set_timecol, 0, 2, 1, fmt_timecol),
    VAL("Brightness", get_bright, set_bright, 10, 100, 10, fmt_pct),
    TOG("USB log", get_usblog, set_usblog),
    ACT("Clear monitor", act_clear),
};

static const menu_item_t settings_items[] = {
    ACT("Date & time", open_datetime),
    VAL("USB power-on", get_usbpo, set_usbpo, USBPO_CHARGE, USBPO_POWER_ON, 1, fmt_usbpo),
    VAL("Auto-off (batt)", get_aoff, set_aoff, 0, AUTOOFF_CHOICES - 1, 1, fmt_aoff),
};

static const menu_item_t stats_items[] = {
    INFO("Battery", fmt_power, 0),
    INFO("Cell", fmt_power, 1),
    INFO("Power", fmt_power, 2),
    INFO("Batt rate", fmt_power, 3),
    INFO("Auto-off", fmt_power, 4),
    INFO("Uptime", fmt_uptime, 0),
    INFO("Last off", fmt_power, 5),
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
    INFO("Log used", fmt_stat, 20),
    INFO("Log sessions", fmt_stat, 21),
    INFO("Log now", fmt_stat, 26),
    INFO("Log drops", fmt_stat, 22),
    INFO("Log bad pages", fmt_stat, 23),
    INFO("Log pages wr", fmt_stat, 24),
    INFO("Log erases", fmt_stat, 25),
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
    ACT("Clear msg counts", act_clear),
};

static const menu_item_t about_items[] = {
    INFO(MIDIUM_NAME, fmt_about, 0),
    INFO("Version", fmt_about, 1),
    INFO("Built", fmt_about, 2),
    INFO("", fmt_about, 3),
    INFO("Display", fmt_about, 4),
    INFO("RTC", fmt_about, 6),
    INFO("Last reset", fmt_about, 5),
    INFO("Last off", fmt_power, 5),
    INFO("Gauge", fmt_power, 6),
    INFO("Unpowered:", NULL, 0),
    INFO(" IN A -> OUT A", NULL, 0),
    INFO(" IN B not passed", NULL, 0),
};

static const menu_t gen_menu      = MENU("Generate", gen_items);
static const menu_t route_menu    = MENU("Routing", route_items);
static const menu_t disp_menu     = MENU("Display", disp_items);
static const menu_t settings_menu = MENU("Settings", settings_items);
static const menu_t stats_menu    = MENU("Stats", stats_items);
static const menu_t about_menu    = MENU("About", about_items);

static const menu_item_t main_items[] = {
    ACT("Monitor", act_monitor),
    ACT("MTC display", open_mtc),
    ACT("Logs", open_logs),
    SUB("Generate", &gen_menu),
    SUB("Routing", &route_menu),
    SUB("Display", &disp_menu),
    SUB("Settings", &settings_menu),
    SUB("Stats", &stats_menu),
    SUB("About", &about_menu),
    ACT("Power off", open_pwrconf),
};
static const menu_t main_menu = MENU("Menu", main_items);

static const char *item_label(const menu_t *m, int idx) {
    const menu_item_t *it = &m->items[idx];
    if (it->label) return it->label;
    if (m == &filter_menu) return msg_filter_cat_label((filter_cat_t)(idx - FILTER_FIRST_CAT));
    return "";
}

/* ------------------------------------------------------------------------ */
/* Navigation                                                                */
/* ------------------------------------------------------------------------ */
static void set_screen(screen_t s) {
    if (s_screen == SCR_MTC && s != SCR_MTC) con_set_gfx_rows(0);
    s_screen = s;
}

static void push_menu(const menu_t *m) {
    if (s_depth + 1 >= MENU_DEPTH) return;
    s_depth++;
    s_stack[s_depth] = m;
    s_sel[s_depth] = 0;
    s_top[s_depth] = 0;
    set_screen(SCR_MENU);
}

static void pop_menu(void) {
    s_editing = false;
    if (s_depth > 0) s_depth--;
    else { s_depth = -1; set_screen(SCR_MONITOR); }
}

/* Leave a full-screen page opened from a menu: back to that menu. */
static void back_to_menu(void) {
    set_screen(s_depth >= 0 ? SCR_MENU : SCR_MONITOR);
}

static void act_monitor(void) {
    s_depth = -1;
    s_editing = false;
    set_screen(SCR_MONITOR);
}

static int menu_rows(void) { return con_rows() - 2; }   /* title + footer */

static void dl_line_cb(const char *t, msg_class_t cls, void *ctx) {
    (void)ctx;
    if (s_dl_n >= DETAIL_MAX_LINES) return;
    copy_trunc(s_dl[s_dl_n], sizeof s_dl[0], t);
    s_dl_cls[s_dl_n] = (uint8_t)cls;
    s_dl_n++;
}

static void open_detail(const midi_msg_t *m, const log_time_t *t, screen_t ret) {
    s_dl_msg = *m;
    if (t) s_dl_time = *t;
    else logger_time(m->t_us, &s_dl_time);
    s_dl_ret = ret;
    s_dl_n = 0;
    s_dl_top = 0;
    unsigned have = m->sx_len < MIDI_SX_STORE ? m->sx_len : MIDI_SX_STORE;
    s_dl_msc = msc_is_msc(m->sx, have);
    dl_line_cb(m->src ? "In   IN B" : "In   IN A", CLS_SYSEX, NULL);
    if (s_dl_time.real) {
        char d[12], tm[16], line[CON_MAX_COLS + 8];
        tu_fmt_date(s_dl_time.real_s, d, sizeof d);
        tu_fmt_tod(s_dl_time.real_s, s_dl_time.real_ms, true, tm, sizeof tm);
        snprintf(line, sizeof line, "At   %s", tm);
        dl_line_cb(line, CLS_NORMAL, NULL);
        snprintf(line, sizeof line, "     %s", d);
        dl_line_cb(line, CLS_DIM, NULL);
    }
    sysex_detail(&s_dl_msg, (unsigned)con_cols(), dl_line_cb, NULL);
    set_screen(SCR_DETAIL);
}

/* UP/DOWN step for the held key (acceleration on big ranges). */
static int step_for(int step, int lo, int hi) { return val_accel_step(step, lo, hi, s_rep_n); }

static void menu_event(joy_event_t e) {
    const menu_t *m = s_stack[s_depth];
    const menu_item_t *it = &m->items[s_sel[s_depth]];

    if (s_editing && it->kind == MI_CUE) {
        int v = s_cue_field ? g_settings.msc_cue_dec : g_settings.msc_cue_int;
        int hi = s_cue_field ? 99 : 9999;
        switch (e) {
        case JOY_UP:    v = val_wrap_step(v, step_for(1, 0, hi), 0, hi); break;
        case JOY_DOWN:  v = val_wrap_step(v, -step_for(1, 0, hi), 0, hi); break;
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
        case JOY_UP:   it->set(val_wrap_step(v, step_for(it->step, it->min, it->max), it->min, it->max)); break;
        case JOY_DOWN: it->set(val_wrap_step(v, -step_for(it->step, it->min, it->max), it->min, it->max)); break;
        default:       s_editing = false; break;
        }
        return;
    }

    switch (e) {
    case JOY_UP:
        s_sel[s_depth] = (uint8_t)val_wrap_step(s_sel[s_depth], -1, 0, m->count - 1);
        break;
    case JOY_DOWN:
        s_sel[s_depth] = (uint8_t)val_wrap_step(s_sel[s_depth], 1, 0, m->count - 1);
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
    if (s_screen != SCR_MENU || s_depth < 0) return;
    /* keep the selection visible */
    int rows = menu_rows();
    if (s_sel[s_depth] < s_top[s_depth]) s_top[s_depth] = s_sel[s_depth];
    if (s_sel[s_depth] >= s_top[s_depth] + rows) s_top[s_depth] = (uint8_t)(s_sel[s_depth] - rows + 1);
}

/* ------------------------------------------------------------------------ */
/* Monitor                                                                   */
/* ------------------------------------------------------------------------ */
static int monitor_rows(void) { return con_rows() - 1; }

/* Pinned mode: clamp the paused view to what is still in the history. */
static void pin_clamp(void) {
    uint32_t next = monitor_next_seq(), oldest = monitor_oldest_seq();
    if (next < 2 || next - 2 < oldest) { s_anchor_none = true; s_cur_pin = true; return; }
    s_anchor_none = false;
    if (s_anchor > next - 2) s_anchor = next - 2;
    if (s_anchor < oldest) s_anchor = oldest;
    if (!s_cur_pin) {
        if (s_cursor < oldest) s_cursor = oldest;
        if (s_cursor > s_anchor) s_cursor = s_anchor;
        uint32_t hist = (uint32_t)(monitor_rows() - 1);
        if (hist == 0) hist = 1;
        if (s_anchor - s_cursor >= hist) s_anchor = s_cursor + hist - 1u;
    }
}

static void monitor_event_pinned(joy_event_t e) {
    uint32_t next = monitor_next_seq();
    uint32_t oldest = monitor_oldest_seq();
    uint32_t hist = (uint32_t)(monitor_rows() - 1);
    switch (e) {
    case JOY_UP:
        if (!s_paused) {
            s_paused = true;
            s_cur_pin = true;
            s_anchor = next >= 2 ? next - 2 : 0;
        } else if (s_cur_pin) {
            pin_clamp();
            if (!s_anchor_none) { s_cur_pin = false; s_cursor = s_anchor; }
        } else if (s_cursor > oldest) {
            s_cursor--;
            if (s_anchor - s_cursor >= hist) s_anchor--;
        }
        break;
    case JOY_DOWN:
        if (!s_paused || s_cur_pin) break;
        if (s_cursor < s_anchor) s_cursor++;
        else if (next >= 2 && s_anchor + 1u <= next - 2) { s_anchor++; s_cursor++; }
        else s_cur_pin = true;
        break;
    case JOY_LEFT:
        s_paused = false;
        break;
    case JOY_RIGHT:
        g_settings.time_col = (uint8_t)val_wrap_step(g_settings.time_col, 1, TIMECOL_OFF, TIMECOL_TOD);
        break;
    case JOY_PRESS: {
        midi_msg_t m;
        uint32_t sel = s_cur_pin ? next - 1u : s_cursor;
        if (s_paused && next && monitor_get(sel, &m) && m.type == MT_SYSEX) open_detail(&m, NULL, SCR_MONITOR);
        else push_menu(&main_menu);
        break;
    }
    default:
        break;
    }
    if (s_paused) pin_clamp();
}

static void monitor_event(joy_event_t e) {
    uint32_t next = monitor_next_seq();
    uint32_t oldest = monitor_oldest_seq();
    if (next == 0 && e != JOY_PRESS && e != JOY_RIGHT) return;
    if (g_settings.pin_latest) { monitor_event_pinned(e); return; }
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
        g_settings.time_col = (uint8_t)val_wrap_step(g_settings.time_col, 1, TIMECOL_OFF, TIMECOL_TOD);
        break;
    case JOY_PRESS: {
        midi_msg_t m;
        if (s_paused && monitor_get(s_anchor, &m) && m.type == MT_SYSEX) open_detail(&m, NULL, SCR_MONITOR);
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
    case JOY_LEFT:  set_screen(s_dl_ret); break;
    default: break;
    }
}

/* ---- message lines (monitor and log browser) ---- */
typedef struct {
    midi_msg_t m;
    log_time_t t;
} vmsg_t;

static int time_col_width(void) {
    switch (g_settings.time_col) {
    case TIMECOL_UPTIME: return 10;                       /* "mm:ss.mmm " */
    case TIMECOL_TOD:    return con_cols() >= 26 ? 13 : 9; /* "hh:mm:ss.mmm " / "hh:mm:ss " */
    default:             return 0;
    }
}

static msg_class_t format_line(const vmsg_t *v, char *line, size_t n) {
    int width = con_cols() - 2 - time_col_width();
    decode_opts_t o = { .note_c3 = g_settings.note_c3, .width = (uint8_t)(width > 0 ? width : 1) };
    char txt[MIDI_TEXT_MAX];
    msg_class_t cls = midi_decode(&v->m, &o, txt, sizeof txt);
    char tag = v->m.src ? 'B' : 'A';
    char t[16];
    switch (g_settings.time_col) {
    case TIMECOL_UPTIME:
        midi_format_time(v->m.t_us, t, sizeof t);
        snprintf(line, n, "%c %s %s", tag, t, txt);
        break;
    case TIMECOL_TOD:
        if (v->t.real) {
            tu_fmt_tod(v->t.real_s, v->t.real_ms, con_cols() >= 26, t, sizeof t);
        } else {
            /* clock not set: uptime in the same width */
            char u[16];
            midi_format_time(v->m.t_us, u, sizeof u);
            if (con_cols() >= 26) snprintf(t, sizeof t, "%12s", u);
            else snprintf(t, sizeof t, "%.8s", u);
        }
        snprintf(line, n, "%c %s %s", tag, t, txt);
        break;
    default:
        snprintf(line, n, "%c %s", tag, txt);
        break;
    }
    return cls;
}

static bool live_msg(uint32_t seq, vmsg_t *v) {
    if (!monitor_get(seq, &v->m)) return false;
    logger_time(v->m.t_us, &v->t);
    return true;
}

static void draw_msg_row(int screen_row, const vmsg_t *v, uint8_t extra) {
    char line[CON_MAX_COLS + 24];
    msg_class_t cls = format_line(v, line, sizeof line);
    if (extra & ATTR_CURSOR) line[1] = '>';      /* cursor: "A>" (plus bar / underline) */
    con_row(screen_row, (uint8_t)((uint8_t)cls | extra), line);
}

/* Header: routing summary, or MTC time while MTC is running (and for
 * MTC_HOLD_US after it stops, marked STOP). */
static bool mtc_header(uint64_t now, char *hdr, size_t n, uint8_t *attr) {
    int in = monitor_mtc_active();
    if (in < 0) return false;
    const mtc_t *m = monitor_mtc(in);
    uint64_t last = mtc_last_us(m);
    if (!m->valid || !last || now - last >= MTC_HOLD_US) return false;
    bool run = mtc_running(m, now);
    char tc[16];
    mtc_tc_str(&m->tc, m->rate, tc, sizeof tc);
    char L = in ? 'B' : 'A';
    if (con_cols() >= 26) {
        static const char *const R[4] = { "24fps", "25fps", "29.97df", "30fps" };
        if (run) snprintf(hdr, n, "%s %s%s %c", tc, R[m->rate & 3u], m->dir < 0 ? " rev" : "", L);
        else snprintf(hdr, n, "%s STOP %c", tc, L);
    } else {
        static const char *const R[4] = { "24", "25", "29D", "30" };
        if (run) snprintf(hdr, n, "%s %s%s %c", tc, R[m->rate & 3u], m->dir < 0 ? "R" : "", L);
        else snprintf(hdr, n, "%s STOP %c", tc, L);
    }
    *attr = (uint8_t)((run ? CLS_HEADER : CLS_DIM) | ATTR_INVERSE);
    return true;
}

/* Battery field for the monitor header: an icon (charge level, or a bolt
 * while charging) and the percentage, 4 cells on the OLED (" 82"), 5 on the
 * TFT (" 82%"). While charging the number alternates with CHG every 2 s; at
 * or below BATT_WARN_PCT on battery it alternates with LOW every second,
 * out of the header's inverse (red on the TFT). Returns the width, 0 = no
 * gauge (no battery UI at all). */
static int batt_field(uint64_t now, char *b, size_t n, uint8_t *attr) {
    const batt_info_t *bi = power_batt();
    if (!bi->present) return 0;
    bool wide = con_cols() >= 26;
    int w = wide ? 4 : 3;
    char num[8];
    snprintf(num, sizeof num, wide ? "%u%%" : "%u", bi->soc_pct);
    const char *txt = num;
    char icon = bi->charging ? CH_BOLT : CH_BATT(batt_level(bi->soc_pct));
    if (power_batt_warn()) {
        *attr = CLS_ERROR;
        if ((now / 1000000u) & 1u) txt = "LOW";
    } else if (bi->charging && ((now / 2000000u) & 1u)) {
        txt = "CHG";
    }
    snprintf(b, n, "%c%*s", icon, w, txt);
    return w + 1;
}

static void render_header(uint64_t now, bool paused) {
    char hdr[CON_MAX_COLS + 24], bf[12];
    int cols = con_cols();
    bool wide = cols >= 26;
    bool act = (uint32_t)(now / 1000u) - midi_io_last_rx_ms() < 150u && midi_io_last_rx_ms() != 0;
    uint8_t attr = CLS_HEADER | ATTR_INVERSE;
    uint8_t battr = attr;
    int bw = batt_field(now, bf, sizeof bf, &battr);
    if (!mtc_header(now, hdr, sizeof hdr, &attr)) {
        char ra[4], rb[4];
        route_summary(&g_settings.route, ME_OUT_A, ra, sizeof ra);
        route_summary(&g_settings.route, ME_OUT_B, rb, sizeof rb);
        /* the OLED drops one space to make room for the battery */
        snprintf(hdr, sizeof hdr, (bw && !wide) ? "%-6s A:%-2s B:%s" : "%-7s A:%-2s B:%s",
                 paused ? "PAUSED" : MIDIUM_NAME, ra, rb);
        con_row(0, attr, hdr);
        if (wide && gen_clock_running()) con_print(0, cols - 3, attr, "C");
        /* TFT: battery before the clock marker; OLED: before the activity star */
        if (bw) con_print(0, cols - 1 - bw - (wide ? 2 : 0), battr, bf);
    } else {
        con_row(0, attr, hdr);
        if (battr == (CLS_HEADER | ATTR_INVERSE)) battr = attr;   /* dimmed with a stopped MTC */
        int len = (int)strlen(hdr);
        if (bw && len < cols - 1 - bw) con_print(0, cols - 1 - bw, battr, bf);
        else if (bw && len < cols - 2) { bf[1] = 0; con_print(0, cols - 2, battr, bf); }   /* icon only */
    }
    con_print(0, cols - 1, attr, act ? "*" : " ");
}

static void render_monitor(uint64_t now) {
    render_header(now, s_paused);
    uint32_t next = monitor_next_seq();
    uint32_t oldest = monitor_oldest_seq();
    int rows = monitor_rows();
    int last_row = con_rows() - 1;
    vmsg_t v;

    if (next == 0) {
        for (int r = 1; r < last_row; r++) con_row(r, CLS_DIM, "");
        con_row(last_row, CLS_DIM, "  waiting for MIDI...");
        return;
    }

    if (g_settings.pin_latest) {
        if (s_paused) pin_clamp();
        /* pinned newest line */
        if (live_msg(next - 1u, &v))
            draw_msg_row(last_row, &v, (uint8_t)(ATTR_INVERSE | (s_paused && s_cur_pin ? ATTR_CURSOR : 0)));
        /* history above it */
        bool have = next >= 2 && next - 2 >= oldest && !(s_paused && s_anchor_none);
        uint32_t bottom = s_paused ? s_anchor : next - 2u;
        for (int r = 0; r < rows - 1; r++) {
            int screen_row = last_row - 1 - r;
            if (!have || bottom < (uint32_t)r || bottom - (uint32_t)r < oldest || !live_msg(bottom - (uint32_t)r, &v)) {
                con_row(screen_row, CLS_DIM, "");
                continue;
            }
            bool sel = s_paused && !s_cur_pin && bottom - (uint32_t)r == s_cursor;
            draw_msg_row(screen_row, &v, sel ? ATTR_CURSOR : 0);
        }
        return;
    }

    if (s_paused) {
        if (s_anchor < oldest) s_anchor = oldest;           /* history overwritten */
        if (s_anchor >= next) s_anchor = next - 1u;
    }
    uint32_t bottom = s_paused ? s_anchor : next - 1u;
    for (int r = 0; r < rows; r++) {
        int screen_row = last_row - r;
        if (bottom < (uint32_t)r || !live_msg(bottom - (uint32_t)r, &v)) {
            con_row(screen_row, CLS_DIM, "");
            continue;
        }
        draw_msg_row(screen_row, &v, (s_paused && r == 0) ? ATTR_INVERSE : 0);
    }
}

/* ------------------------------------------------------------------------ */
/* Menus                                                                     */
/* ------------------------------------------------------------------------ */
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
        char v[16];
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

static void render_footer(uint64_t now, const char *hint) {
    const char *foot = hint;
    uint8_t fattr = CLS_DIM;
    if (toast_live(now)) { foot = s_toast; fattr = CLS_PC; }
    else if (gen_last_text()[0] && now - gen_last_time_us() < 3000000u) { foot = gen_last_text(); fattr = CLS_PC; }
    con_row(con_rows() - 1, fattr, foot);
}

static void render_menu(uint64_t now) {
    const menu_t *m = s_stack[s_depth];
    int rows = menu_rows();
    char title[CON_MAX_COLS + 8];
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
        con_row_lr(1 + r, attr, item_label(m, idx), val);
    }

    const menu_item_t *sel = &m->items[s_sel[s_depth]];
    if (s_editing && sel->kind == MI_CUE) con_row(con_rows() - 1, CLS_DIM, s_cue_field ? "UP/DN adj  LEFT int" : "UP/DN adj  RIGHT .dec");
    else if (s_editing) con_row(con_rows() - 1, CLS_DIM, "UP/DN adjust, PRESS ok");
    else render_footer(now, con_cols() >= 26 ? "PRESS select  LEFT back" : "PRESS sel  LEFT back");
}

static void render_detail(void) {
    char left[CON_MAX_COLS + 24], right[12], t[16];
    int rows = detail_rows();
    unsigned pages = s_dl_n ? (s_dl_n + (unsigned)rows - 1u) / (unsigned)rows : 1u;
    unsigned page = s_dl_top / (unsigned)rows + 1u;
    if (s_dl_top + (unsigned)rows >= s_dl_n) page = pages;       /* last screen */
    if (s_dl_time.real) tu_fmt_tod(s_dl_time.real_s, s_dl_time.real_ms, con_cols() >= 26, t, sizeof t);
    else midi_format_time(s_dl_msg.t_us, t, sizeof t);
    snprintf(left, sizeof left, "%s %c %s", s_dl_msc ? "MSC" : "SYX", s_dl_msg.src ? 'B' : 'A', t);
    snprintf(right, sizeof right, "%u/%u", page, pages);
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, left, right);
    for (int r = 0; r < rows; r++) {
        unsigned i = s_dl_top + (unsigned)r;
        if (i < s_dl_n) con_row(1 + r, s_dl_cls[i], s_dl[i]);
        else con_row(1 + r, CLS_NORMAL, "");
    }
}

/* ------------------------------------------------------------------------ */
/* MTC display                                                               */
/* ------------------------------------------------------------------------ */
typedef struct {
    int row0, nrows;              /* bitmap band in text rows */
    int x0, y_off;                /* first glyph x; glyph y within the band */
    int dw, dh, t, cw, gap;       /* digit w / h, stroke, colon w, gap inside a pair */
} big_layout_t;

static void big_layout(big_layout_t *L) {
    const display_driver_t *d = con_driver();
    int gw = con_cols() * d->cell_w;
    if (d->cell_h >= 16) {          /* TFT 312 x 160 grid */
        *L = (big_layout_t){ 1, 6, 0, 8, 28, 80, 7, 18, 6 };
    } else {                        /* OLED 126 x 64 grid */
        *L = (big_layout_t){ 1, 5, 0, 3, 11, 34, 3, 7, 3 };
    }
    int total = 8 * L->dw + 3 * L->cw + 4 * L->gap;
    L->x0 = (gw - total) / 2;
}

static int glyph_x(const big_layout_t *L, int i, int *w) {
    /* "hh:mm:ss:ff": digits at 0,1,3,4,6,7,9,10; separators at 2,5,8 */
    int x = L->x0;
    for (int k = 0; k < i; k++) {
        bool sep = (k % 3) == 2;
        x += sep ? L->cw : L->dw + (((k % 3) == 0) ? L->gap : 0);
    }
    *w = (i % 3) == 2 ? L->cw : L->dw;
    return x;
}

static uint8_t s_gbuf[8 * 96];      /* one glyph: up to 64 px wide x 96 px */

static void big_draw(const char *s, uint8_t cls, bool force) {
    const display_driver_t *d = con_driver();
    if (!d || !d->draw_bitmap) return;
    big_layout_t L;
    big_layout(&L);
    int bh = L.nrows * d->cell_h, by = L.row0 * d->cell_h;
    if (force) {
        /* clear the band, glyph-cell wide strips (s_gbuf sized for one glyph) */
        memset(s_gbuf, 0, sizeof s_gbuf);
        int gw = con_cols() * d->cell_w;
        for (int x = 0; x < gw; x += 64) d->draw_bitmap(x, by, gw - x < 64 ? gw - x : 64, bh, s_gbuf, 8, cls);
    }
    for (int i = 0; i < 11 && s[i]; i++) {
        if (!force && s_big_last[i] == s[i]) continue;
        int w, x = glyph_x(&L, i, &w);
        int stride = (w + 7) / 8;
        memset(s_gbuf, 0, sizeof s_gbuf);
        bd_glyph(s_gbuf, stride, 0, L.y_off, w, L.dh, L.t, s[i]);
        d->draw_bitmap(x, by, w, bh, s_gbuf, stride, cls);
    }
    snprintf(s_big_last, sizeof s_big_last, "%s", s);
}

static void open_mtc(void) {
    big_layout_t L;
    if (!con_driver() || !con_driver()->draw_bitmap) return;
    big_layout(&L);
    set_screen(SCR_MTC);
    con_set_gfx_rows(((1u << L.nrows) - 1u) << L.row0);
    s_big_force = true;
}

static void render_mtc(uint64_t now) {
    big_layout_t L;
    big_layout(&L);
    int in = monitor_mtc_active();
    const mtc_t *m = in >= 0 ? monitor_mtc(in) : NULL;
    bool valid = m && m->valid;
    bool run = valid && mtc_running(m, now);
    char tc[16];
    if (valid) mtc_tc_str(&m->tc, m->rate, tc, sizeof tc);
    else snprintf(tc, sizeof tc, "--:--:--:--");
    uint8_t cls = run ? CLS_HEADER : CLS_DIM;
    bool force = s_big_force || cls != s_big_cls;
    s_big_force = false;
    s_big_cls = cls;
    big_draw(tc, cls, force);

    bool wide = con_cols() >= 26;
    char l[CON_MAX_COLS + 8], r[CON_MAX_COLS + 8];
    snprintf(l, sizeof l, "MTC");
    snprintf(r, sizeof r, "%s", in < 0 ? "" : in ? "IN B" : "IN A");
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, l, r);
    int st_row = L.row0 + L.nrows;
    const char *state = !valid ? "NO MTC" : run ? (m->dir < 0 ? "RUNNING  REV <<" : "RUNNING  FWD >>")
                                               : m->last_qf_us ? "STOPPED" : "LOCATED (full fr)";
    if (!wide && valid && run) state = m->dir < 0 ? "RUN  << REV" : "RUN  >> FWD";
    if (!wide && valid && !run && !m->last_qf_us) state = "LOCATED";
    if (valid) {
        static const char *const R[4] = { "24 fps", "25 fps", "29.97 DF", "30 fps" };
        snprintf(r, sizeof r, "%s", R[m->rate & 3u]);
    } else {
        r[0] = 0;
    }
    con_row_lr(st_row, run ? CLS_NOTE : CLS_DIM, state, r);
    if (wide) {
        if (valid) snprintf(l, sizeof l, "Source IN %c   %s", in ? 'B' : 'A', m->rate == MTC_RATE_2997 ? "drop-frame" : "non-drop");
        else snprintf(l, sizeof l, "Waiting for MTC on IN A / B");
        con_row(st_row + 1, CLS_NORMAL, l);
        con_row(con_rows() - 1, CLS_DIM, "LEFT back");
    } else {
        con_row(con_rows() - 1, CLS_DIM, "LEFT back");
    }
}

/* ------------------------------------------------------------------------ */
/* Date & time                                                               */
/* ------------------------------------------------------------------------ */
static void open_datetime(void) {
    s_dt_edit = false;
    set_screen(SCR_DATETIME);
}

static void dt_now(tu_datetime_t *d) {
    uint32_t s;
    uint16_t ms;
    if (clock_real(time_us_64(), &s, &ms)) { tu_from_secs(s, d); return; }
    if (!tu_parse_build(__DATE__, __TIME__, d)) *d = (tu_datetime_t){ 2026, 1, 1, 12, 0, 0 };
}

static void dt_event(joy_event_t e) {
    if (!s_dt_edit) {
        if (e == JOY_LEFT) back_to_menu();
        else if (e == JOY_PRESS || e == JOY_RIGHT) { dt_now(&s_dt); s_dt.sec = 0; s_dt_edit = true; s_dt_field = 0; }
        return;
    }
    int dir = e == JOY_UP ? 1 : e == JOY_DOWN ? -1 : 0;
    if (dir) {
        tu_datetime_t *d = &s_dt;
        switch (s_dt_field) {
        case 0: d->year = (uint16_t)val_wrap_step(d->year, dir * step_for(1, 2000, 2099), 2000, 2099); break;
        case 1: d->month = (uint8_t)val_wrap_step(d->month, dir, 1, 12); break;
        case 2: d->day = (uint8_t)val_wrap_step(d->day, dir, 1, tu_days_in_month(d->year, d->month)); break;
        case 3: d->hour = (uint8_t)val_wrap_step(d->hour, dir, 0, 23); break;
        case 4: d->min = (uint8_t)val_wrap_step(d->min, dir, 0, 59); break;
        default: d->sec = (uint8_t)val_wrap_step(d->sec, dir, 0, 59); break;
        }
        uint8_t dim = tu_days_in_month(d->year, d->month);
        if (d->day > dim) d->day = dim;
        return;
    }
    switch (e) {
    case JOY_LEFT:  if (s_dt_field == 0) s_dt_edit = false; else s_dt_field--; break;
    case JOY_RIGHT: if (s_dt_field < 5) s_dt_field++; break;
    case JOY_PRESS:
        if (clock_set(&s_dt)) toast(clock_rtc_present() ? "Time set (RTC)" : "Time set");
        else toast("Could not set the RTC");
        s_dt_edit = false;
        break;
    default: break;
    }
}

static void render_datetime(uint64_t now) {
    char l[CON_MAX_COLS + 16], r[CON_MAX_COLS + 16];
    bool wide = con_cols() >= 26;
    con_row(0, CLS_HEADER | ATTR_INVERSE, "Date & time");
    snprintf(r, sizeof r, "%s", clock_rtc_present() ? "RV-3028" : wide ? "none (software)" : "none (soft)");
    con_row_lr(1, CLS_NORMAL, "RTC", r);
    snprintf(r, sizeof r, "%s", clock_valid() ? "valid" : clock_lost() ? "lost - set it" : "not set");
    con_row_lr(2, clock_valid() ? CLS_NORMAL : CLS_ERROR, "Time", r);
    if (clock_rtc_present()) {
        uint8_t b = rv3028_backup_mode();
        snprintf(r, sizeof r, "%s", b == RV3028_BSM_DSM ? "direct (DSM)" : b == 0x0C ? "level (LSM)" : "off");
        con_row_lr(3, CLS_DIM, "Backup", r);
    } else {
        con_row(3, CLS_DIM, "Lost at power off");
    }
    con_row(4, CLS_NORMAL, "");
    tu_datetime_t d;
    if (s_dt_edit) d = s_dt;
    else dt_now(&d);
    unsigned v[6] = { d.year, d.month, d.day, d.hour, d.min, d.sec };
    static const char sep[6] = { '-', '-', ' ', ':', ':', 0 };
    int k = 0;
    for (int f = 0; f < 6; f++) {
        bool cur = s_dt_edit && f == s_dt_field;
        k += snprintf(l + k, sizeof l - (size_t)k, f == 0 ? (cur ? "<%04u>" : "%04u") : (cur ? "<%02u>" : "%02u"), v[f]);
        if (sep[f] && k < (int)sizeof l - 1) { l[k++] = sep[f]; l[k] = 0; }
    }
    char line[CON_MAX_COLS + 24];
    int pad = (con_cols() - (int)strlen(l)) / 2;
    if (pad < 0) pad = 0;
    snprintf(line, sizeof line, "%*s%s", pad, "", l);
    uint8_t attr = s_dt_edit ? CLS_PC : (clock_valid() ? CLS_NORMAL : CLS_DIM);
    con_row(5, attr, (s_dt_edit || clock_valid()) ? line : (wide ? "      -- not set --" : "    -- not set --"));
    for (int rr = 6; rr < con_rows() - 1; rr++) con_row(rr, CLS_NORMAL, "");
    if (s_dt_edit) con_row(con_rows() - 1, CLS_DIM, wide ? "U/D adj L/R field PRESS ok" : "UD adj LR fld PRS ok");
    else render_footer(now, wide ? "PRESS set time  LEFT back" : "PRESS set  LEFT back");
}

/* ------------------------------------------------------------------------ */
/* Logs                                                                      */
/* ------------------------------------------------------------------------ */
static int logs_items(void) { return log_session_count(logger_log()) + 2; }   /* New, sessions..., Delete */

static void open_logs(void) {
    s_lg_sel = 0;
    s_lg_top = 0;
    set_screen(SCR_LOGS);
}

/* Lines an item takes on the list. */
static int item_lines(int i) { return (i == 0 || i == logs_items() - 1) ? 1 : 2; }

static void logs_scroll(void) {
    int rows = con_rows() - 2;
    if (s_lg_sel < s_lg_top) s_lg_top = s_lg_sel;
    for (;;) {
        int used = 0;
        for (int i = s_lg_top; i <= s_lg_sel; i++) used += item_lines(i);
        if (used <= rows || s_lg_top >= s_lg_sel) break;
        s_lg_top++;
    }
}

static bool browse_visible(const midi_msg_t *m) { return msg_filter_pass(&g_settings.filter, m); }

/* Step to the previous / next record that passes the filters (bounded). */
#define BROWSE_SCAN_MAX 20000
static bool browse_step(log_pos_t *p, int dir) {
    log_t *Lg = logger_log();
    log_pos_t q = *p;
    midi_msg_t m;
    for (int k = 0; k < BROWSE_SCAN_MAX; k++) {
        bool ok = dir < 0 ? log_pos_prev(Lg, s_br_no, &q) : log_pos_next(Lg, s_br_no, &q);
        if (!ok) return false;
        if (log_read(Lg, &q, &m, NULL) && browse_visible(&m)) { *p = q; return true; }
    }
    return false;
}

static void open_browse(uint32_t no) {
    log_t *Lg = logger_log();
    if (log_is_live(Lg, no)) logger_flush_now();
    s_br_no = no;
    s_br_have = log_pos_last(Lg, no, &s_br_pos);
    if (s_br_have) {
        midi_msg_t m;
        if (!(log_read(Lg, &s_br_pos, &m, NULL) && browse_visible(&m))) s_br_have = browse_step(&s_br_pos, -1);
    }
    set_screen(SCR_BROWSE);
}

static void logs_event(joy_event_t e) {
    log_t *Lg = logger_log();
    if (log_wiping(Lg, NULL)) { if (e == JOY_LEFT) back_to_menu(); return; }
    int n = logs_items();
    switch (e) {
    case JOY_UP:   s_lg_sel = val_wrap_step(s_lg_sel, -1, 0, n - 1); break;
    case JOY_DOWN: s_lg_sel = val_wrap_step(s_lg_sel, 1, 0, n - 1); break;
    case JOY_LEFT: back_to_menu(); return;
    case JOY_RIGHT:
    case JOY_PRESS:
        if (!logger_enabled()) break;
        if (s_lg_sel == 0) {
            char t[CON_MAX_COLS + 8];
            logger_new_session();
            snprintf(t, sizeof t, "New session #%lu", (unsigned long)Lg->live_no);
            toast(t);
        } else if (s_lg_sel == n - 1) {
            s_del_sel = 0;
            set_screen(SCR_LOGDEL);
        } else {
            const log_sess_t *se = log_session(Lg, s_lg_sel - 1);
            if (se) open_browse(se->no);
        }
        break;
    default: break;
    }
    if (s_lg_sel >= logs_items()) s_lg_sel = logs_items() - 1;
    logs_scroll();
}

static void sess_title(const log_sess_t *e, char *b, size_t n) {
    char d[16], t[16];
    bool wide = con_cols() >= 26;
    const char *pre = e->partial ? "~" : "";
    if (e->start.real) {
        tu_fmt_date(e->start.real_s, d, sizeof d);
        tu_fmt_tod(e->start.real_s, 0, false, t, sizeof t);
        if (wide) snprintf(b, n, "%s#%lu %s %s", pre, (unsigned long)e->no, d, t);
        else snprintf(b, n, "%s#%lu %.5s %.5s", pre, (unsigned long)e->no, d + 5, t);
    } else {
        tu_fmt_duration((uint32_t)(e->start.t_us / 1000000u), t, sizeof t);
        snprintf(b, n, "%s#%lu up %s", pre, (unsigned long)e->no, t);
    }
}

static void sess_info(const log_sess_t *e, char *b, size_t n) {
    uint32_t dur = 0;
    if (e->has_end) {
        if (e->start.real && e->end.real && e->end.real_s >= e->start.real_s) dur = e->end.real_s - e->start.real_s;
        else if (e->end.t_us >= e->start.t_us) dur = (uint32_t)((e->end.t_us - e->start.t_us) / 1000000u);
    }
    char d[16];
    tu_fmt_duration(dur, d, sizeof d);
    snprintf(b, n, "  %s  %lu msg%s", d, (unsigned long)e->nrec, e->nrec == 1 ? "" : "s");
}

static void render_logs(uint64_t now) {
    log_t *Lg = logger_log();
    char l[CON_MAX_COLS + 24], r[16];
    if (logger_enabled()) snprintf(r, sizeof r, "%u%% used", log_used_pct(Lg));
    else snprintf(r, sizeof r, "off");
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, "Logs", r);
    int rows = con_rows() - 2;
    unsigned pct;
    if (log_wiping(Lg, &pct)) {
        for (int i = 1; i <= rows; i++) con_row(i, CLS_NORMAL, "");
        snprintf(l, sizeof l, "Deleting... %u%%", pct);
        con_row(2, CLS_ERROR, l);
        con_row(con_rows() - 1, CLS_DIM, "please wait");
        return;
    }
    if (!logger_enabled()) {
        for (int i = 1; i <= rows; i++) con_row(i, CLS_NORMAL, "");
        con_row(2, CLS_ERROR, "Logging is off");
        con_row(3, CLS_DIM, "(no log flash)");
        render_footer(now, "LEFT back");
        return;
    }
    int n = logs_items();
    if (s_lg_sel >= n) s_lg_sel = n - 1;
    logs_scroll();
    int row = 1;
    for (int i = s_lg_top; i < n && row <= rows; i++) {
        uint8_t attr = (uint8_t)(i == s_lg_sel ? ATTR_INVERSE : 0);
        if (i == 0) { con_row(row++, (uint8_t)(CLS_NORMAL | attr), "+ New log session"); continue; }
        if (i == n - 1) { con_row(row++, (uint8_t)(CLS_ERROR | attr), "x Delete all logs"); continue; }
        const log_sess_t *e = log_session(Lg, i - 1);
        if (!e) continue;
        bool live = log_is_live(Lg, e->no);
        bool wide = con_cols() >= 26;
        sess_title(e, l, sizeof l);
        con_row_lr(row++, (uint8_t)((live ? CLS_NOTE : CLS_NORMAL) | attr), l, live && !wide ? "LIVE" : "");
        if (row <= rows) {
            sess_info(e, l, sizeof l);
            con_row(row++, (uint8_t)(CLS_DIM | attr), l);
            if (live && wide) con_print(row - 1, con_cols() - 4, (uint8_t)(CLS_NOTE | attr), "LIVE");
        }
    }
    while (row <= rows) con_row(row++, CLS_NORMAL, "");
    render_footer(now, "PRESS open  LEFT back");
}

static void logdel_event(joy_event_t e) {
    switch (e) {
    case JOY_UP:
    case JOY_DOWN: s_del_sel = (uint8_t)val_wrap_step(s_del_sel, 1, 0, 1); break;
    case JOY_LEFT: set_screen(SCR_LOGS); break;
    case JOY_RIGHT:
    case JOY_PRESS:
        if (s_del_sel == 1) { logger_delete_all(); s_lg_sel = 0; s_lg_top = 0; toast("Logs deleted"); }
        set_screen(SCR_LOGS);
        break;
    default: break;
    }
}

static void render_logdel(void) {
    log_t *Lg = logger_log();
    char l[CON_MAX_COLS + 24];
    con_row(0, CLS_ERROR | ATTR_INVERSE, "Delete all logs?");
    snprintf(l, sizeof l, "%d sessions, %u%% used", log_session_count(Lg), log_used_pct(Lg));
    con_row(1, CLS_NORMAL, l);
    con_row(2, CLS_DIM, "Cannot be undone.");
    con_row(3, CLS_NORMAL, "");
    con_row(4, (uint8_t)(CLS_NORMAL | (s_del_sel == 0 ? ATTR_INVERSE : 0)), "  Cancel");
    con_row(5, (uint8_t)(CLS_ERROR | (s_del_sel == 1 ? ATTR_INVERSE : 0)), "  Delete all logs");
    for (int r = 6; r < con_rows() - 1; r++) con_row(r, CLS_NORMAL, "");
    con_row(con_rows() - 1, CLS_DIM, "UP/DN choose PRESS ok");
}

static void browse_event(joy_event_t e) {
    switch (e) {
    case JOY_UP:   if (s_br_have) browse_step(&s_br_pos, -1); break;
    case JOY_DOWN: if (s_br_have) browse_step(&s_br_pos, +1); break;
    case JOY_LEFT: set_screen(SCR_LOGS); break;
    case JOY_RIGHT:
        g_settings.time_col = (uint8_t)val_wrap_step(g_settings.time_col, 1, TIMECOL_OFF, TIMECOL_TOD);
        break;
    case JOY_PRESS: {
        vmsg_t v;
        if (s_br_have && log_read(logger_log(), &s_br_pos, &v.m, &v.t) && v.m.type == MT_SYSEX)
            open_detail(&v.m, &v.t, SCR_BROWSE);
        break;
    }
    default: break;
    }
}

static void render_browse(void) {
    log_t *Lg = logger_log();
    const log_sess_t *e = log_find(Lg, s_br_no);
    char l[CON_MAX_COLS + 24], r[CON_MAX_COLS + 8];
    snprintf(l, sizeof l, "LOG #%lu", (unsigned long)s_br_no);
    r[0] = 0;
    if (e && log_is_live(Lg, s_br_no)) snprintf(r, sizeof r, "LIVE");
    else if (e && e->start.real) tu_fmt_date(e->start.real_s, r, sizeof r);
    con_row_lr(0, CLS_HEADER | ATTR_INVERSE, l, r);
    int rows = con_rows() - 1;
    int last_row = con_rows() - 1;
    if (!e || !s_br_have) {
        for (int i = 1; i < last_row; i++) con_row(i, CLS_DIM, "");
        con_row(last_row, CLS_DIM, e && e->nrec ? "  none match the filters" : "  (empty session)");
        return;
    }
    log_pos_t p = s_br_pos;
    int r0 = 0;
    for (; r0 < rows; r0++) {
        vmsg_t v;
        if (r0 > 0 && !browse_step(&p, -1)) break;
        if (!log_read(Lg, &p, &v.m, &v.t)) break;
        draw_msg_row(last_row - r0, &v, r0 == 0 ? ATTR_INVERSE : 0);
    }
    for (; r0 < rows; r0++) con_row(last_row - r0, CLS_DIM, "");
}

/* ------------------------------------------------------------------------ */
/* Power: confirm page, countdown, warnings, shutdown / off screens          */
/* ------------------------------------------------------------------------ */
static void open_pwrconf(void) {
    s_pwr_sel = 0;
    set_screen(SCR_PWRCONF);
}

static void pwrconf_event(joy_event_t e) {
    switch (e) {
    case JOY_UP:
    case JOY_DOWN: s_pwr_sel = (uint8_t)val_wrap_step(s_pwr_sel, 1, 0, 1); break;
    case JOY_LEFT: back_to_menu(); break;
    case JOY_RIGHT:
    case JOY_PRESS:
        if (s_pwr_sel == 1) power_request_off(LOG_END_CLEAN);
        else back_to_menu();
        break;
    default: break;
    }
}

static void render_pwrconf(void) {
    bool wide = con_cols() >= 26;
    con_row(0, CLS_ERROR | ATTR_INVERSE, "Power off?");
    con_row(1, CLS_NORMAL, wide ? "The log is saved first." : "Log is saved first.");
    con_row(2, CLS_DIM, wide ? "Off: IN A -> OUT A only" : "Off: IN A->OUT A only");
    con_row(3, CLS_NORMAL, "");
    con_row(4, (uint8_t)(CLS_NORMAL | (s_pwr_sel == 0 ? ATTR_INVERSE : 0)), "  Cancel");
    con_row(5, (uint8_t)(CLS_ERROR | (s_pwr_sel == 1 ? ATTR_INVERSE : 0)), "  Power off");
    for (int r = 6; r < con_rows() - 1; r++) con_row(r, CLS_NORMAL, "");
    con_row(con_rows() - 1, CLS_DIM, "UP/DN choose PRESS ok");
}

/* A whole-screen message: title bar, lines from row 2, footer. */
static void render_panel(uint8_t tattr, const char *title, const char *const *lines, int nl, const char *foot) {
    con_row(0, tattr, title);
    con_row(1, CLS_NORMAL, "");
    int r = 2;
    for (int i = 0; i < nl && r < con_rows() - 1; i++, r++)
        con_row(r, (uint8_t)(i == 0 ? CLS_NORMAL : CLS_DIM), lines[i] ? lines[i] : "");
    for (; r < con_rows() - 1; r++) con_row(r, CLS_NORMAL, "");
    con_row(con_rows() - 1, CLS_DIM, foot ? foot : "");
}

/* Progress bar across row r, permille filled (inverse cells). */
static void render_bar(int r, unsigned permille, uint8_t cls) {
    int cols = con_cols(), w = cols - 2;
    int fill = (int)((permille * (unsigned)w + 500u) / 1000u);
    con_row(r, CLS_DIM, "");
    con_print(r, 0, CLS_DIM, "[");
    for (int i = 0; i < w; i++) con_print(r, 1 + i, i < fill ? (uint8_t)(cls | ATTR_INVERSE) : CLS_DIM, i < fill ? " " : "-");
    con_print(r, cols - 1, CLS_DIM, "]");
}

static void render_hold(uint64_t now) {
    bool wide = con_cols() >= 26;
    uint64_t el = now - s_hold_t, tot = (uint64_t)PWR_COUNTDOWN_MS * 1000u;
    if (el > tot) el = tot;
    unsigned pm = (unsigned)(el * 1000u / tot);
    unsigned left_ds = (unsigned)((tot - el + 99999u) / 100000u);     /* tenths of s, rounded up */
    char t[24];
    con_row(0, CLS_ERROR | ATTR_INVERSE, "Power off");
    con_row(1, CLS_NORMAL, "");
    con_row(2, CLS_NORMAL, wide ? "Power off... keep holding" : "Power off... hold on");
    con_row(3, CLS_NORMAL, "release to cancel");
    con_row(4, CLS_NORMAL, "");
    render_bar(5, pm, CLS_ERROR);
    snprintf(t, sizeof t, "%u.%u s", left_ds / 10u, left_ds % 10u);
    con_row(6, CLS_DIM, "");
    con_print(6, (con_cols() - (int)strlen(t)) / 2, CLS_NORMAL, t);
    for (int r = 7; r < con_rows(); r++) con_row(r, CLS_NORMAL, "");
}

static void render_autooff(void) {
    bool wide = con_cols() >= 26;
    uint32_t left = 0;
    power_autooff_warning(&left);
    char l1[24];
    snprintf(l1, sizeof l1, "Powering off in %lu s", (unsigned long)left);
    const char *lines[] = { l1, wide ? "No MIDI and no keys for" : "No MIDI or keys for", NULL };
    char l2[24];
    snprintf(l2, sizeof l2, "%u min, on battery.", (unsigned)power_autooff_minutes());
    lines[2] = l2;
    render_panel(CLS_PC | ATTR_INVERSE, "Auto power off", lines, 3, "Any key: stay on");
    unsigned span = AUTOOFF_WARN_MS / 1000u, gone = left < span ? span - (unsigned)left : 0u;
    render_bar(con_rows() - 3, gone * 1000u / span, CLS_PC);
}

static void render_lowbatt(void) {
    bool wide = con_cols() >= 26;
    char l1[24], l3[28];
    snprintf(l1, sizeof l1, "%u%% left", power_batt()->soc_pct);
    snprintf(l3, sizeof l3, wide ? "Powers off safely at %u%%" : "Powers off at %u%%", (unsigned)BATT_OFF_PCT);
    const char *lines[] = { l1, "Connect USB-C power", l3 };
    render_panel(CLS_ERROR | ATTR_INVERSE, "Battery low", lines, 3, "PRESS ok");
}

static void render_power(uint64_t now) {
    bool wide = con_cols() >= 26;
    (void)now;
    switch (power_state()) {
    case PWR_SHUTDOWN: {
        uint8_t r = power_off_reason();
        const char *why = r == LOG_END_LOWBATT ? "Battery empty." : r == LOG_END_AUTOOFF ? (wide ? "Auto-off: no activity." : "Auto-off (idle).") : "";
        const char *lines[] = { "Saving the log...", why };
        render_panel(CLS_ERROR | ATTR_INVERSE, "Shutting down", lines, 2, "");
        break;
    }
    case PWR_OFF_MSG:
    case PWR_KILLED: {
        const char *lines[] = { "Safe to unplug.", "", wide ? "Relays: IN A -> OUT A" : "Relays: IN A->OUT A", "(passive thru)" };
        render_panel(CLS_DIM | ATTR_INVERSE, "Off", lines, 4, "");
        break;
    }
    case PWR_IDLE: {
        const char *lines[] = { "Safe to unplug.", "", "Press the joystick", "to power on." };
        render_panel(CLS_DIM | ATTR_INVERSE, "Off", lines, 4, wide ? "Relays: IN A -> OUT A" : "IN A -> OUT A only");
        break;
    }
    case PWR_CHARGE_ONLY: {
        const batt_info_t *bi = power_batt();
        char l1[24];
        if (bi->present) snprintf(l1, sizeof l1, "%c %u%%  %s", bi->charging ? CH_BOLT : CH_BATT(batt_level(bi->soc_pct)),
                                  bi->soc_pct, bi->charging ? "charging" : "full");
        else snprintf(l1, sizeof l1, "USB power");
        const char *lines[] = { l1, "", "Press the joystick", "to power on." };
        render_panel(CLS_NOTE | ATTR_INVERSE, "Charging", lines, 4, "MIDIum is off");
        break;
    }
    default:
        break;
    }
}

/* Which overlay (if any) covers the screen now. */
static overlay_t overlay_now(void) {
    if (power_state() != PWR_RUN) return OV_POWER;
    if (s_hold) return OV_HOLD;
    if (power_autooff_warning(NULL)) return OV_AUTOOFF;
    if (s_lowbatt_popup) return OV_LOWBATT;
    return OV_NONE;
}

/* ------------------------------------------------------------------------ */
/* Events and rendering                                                      */
/* ------------------------------------------------------------------------ */
void ui_event(joy_event_t e) {
    if (e == JOY_NONE) return;
    if (power_state() != PWR_RUN) return;            /* shutting down / off */
    uint64_t now = time_us_64();
    power_activity();
    s_need_render = true;
    switch (e) {
    case JOY_HOLD:        s_hold = true; s_hold_t = now; return;
    case JOY_HOLD_CANCEL: s_hold = false; return;     /* no other effect */
    case JOY_HOLD_OFF:    s_hold = false; power_request_off(LOG_END_CLEAN); return;
    default: break;
    }
    if (s_hold) return;
    /* a key that cancels the auto-off warning or closes the popup does nothing else */
    if (s_ov == OV_AUTOOFF) return;
    if (s_lowbatt_popup) { s_lowbatt_popup = false; return; }
    /* consecutive UP / DOWN events of one held key (auto-repeat) */
    if ((e == JOY_UP || e == JOY_DOWN) && e == s_rep_e && now - s_rep_t < 450000u) s_rep_n++;
    else s_rep_n = 0;
    s_rep_e = e;
    s_rep_t = now;

    switch (s_screen) {
    case SCR_MONITOR:  monitor_event(e); break;
    case SCR_DETAIL:   detail_event(e); break;
    case SCR_MTC:      if (e == JOY_LEFT || e == JOY_PRESS) back_to_menu(); break;
    case SCR_DATETIME: dt_event(e); break;
    case SCR_LOGS:     logs_event(e); break;
    case SCR_LOGDEL:   logdel_event(e); break;
    case SCR_BROWSE:   browse_event(e); break;
    case SCR_PWRCONF:  pwrconf_event(e); break;
    default:           menu_event(e); break;
    }
    s_need_render = true;
}

/* Something that changes faster than the 100 ms refresh: MTC. */
static uint32_t live_signature(uint64_t now) {
    int in = monitor_mtc_active();
    if (in < 0) return 0;
    const mtc_t *m = monitor_mtc(in);
    uint32_t sig = (uint32_t)m->tc.f | (uint32_t)m->tc.s << 8 | (uint32_t)m->tc.m << 16 | (uint32_t)m->tc.h << 24;
    sig ^= mtc_running(m, now) ? 0x80000000u : 0u;
    sig ^= (now - mtc_last_us(m) < MTC_HOLD_US) ? 0x40000000u : 0u;
    return sig ^ (uint32_t)in;
}

void ui_render(uint64_t now, bool history_changed) {
    static uint64_t last;
    static uint32_t last_sig;
    if (power_take_lowbatt_popup()) s_lowbatt_popup = true;
    overlay_t ov = overlay_now();
    if (ov != s_ov) {
        /* the MTC page draws bitmaps: give its rows back to the text console
         * under an overlay, and redraw it whole afterwards */
        if (s_screen == SCR_MTC) {
            if (s_ov == OV_NONE) con_set_gfx_rows(0);
            else if (ov == OV_NONE) open_mtc();
        }
        s_ov = ov;
        s_need_render = true;
    }
    if (ov != OV_NONE) {
        if (!s_need_render && now - last < 100000u) return;
        last = now;
        s_need_render = false;
        switch (ov) {
        case OV_POWER:   if (!power_display_off()) render_power(now); break;
        case OV_HOLD:    render_hold(now); break;
        case OV_AUTOOFF: render_autooff(); break;
        default:         render_lowbatt(); break;
        }
        return;
    }
    uint32_t sig = (s_screen == SCR_MONITOR || s_screen == SCR_MTC) ? live_signature(now) : 0;
    bool sig_changed = sig != last_sig;
    bool hist = history_changed && (s_screen == SCR_MONITOR);
    if (!s_need_render && !hist && !sig_changed && now - last < 100000u) return;
    last = now;
    last_sig = sig;
    s_need_render = false;
    switch (s_screen) {
    case SCR_MONITOR:  render_monitor(now); break;
    case SCR_DETAIL:   render_detail(); break;
    case SCR_MTC:      render_mtc(now); break;
    case SCR_DATETIME: render_datetime(now); break;
    case SCR_LOGS:     render_logs(now); break;
    case SCR_LOGDEL:   render_logdel(); break;
    case SCR_BROWSE:   render_browse(); break;
    case SCR_PWRCONF:  render_pwrconf(); break;
    default:           render_menu(now); break;
    }
}

void ui_init(void) {
    const display_driver_t *d = con_driver();
    s_disp_name = d ? d->name : "none";
    if (d && d->set_brightness) d->set_brightness(g_settings.brightness);
    s_screen = SCR_MONITOR;
    s_need_render = true;
}
