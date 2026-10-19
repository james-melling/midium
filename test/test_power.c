/*
 * test_power.c - v0.5 soft power and battery: the power-button state machine
 * (tap / hold / cancel / off, no spurious select), auto-off timing, low
 * battery thresholds, MAX17048 register conversions, and the log's session
 * close record (clean shutdown vs power loss).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_common.h"
#include "power_fsm.h"
#include "max17048.h"
#include "log_store.h"
#include "log_flash_ram.h"
#include "midi_defs.h"

/* Run the button machine from t0 to t1 (inclusive, 1 ms steps) with the
 * switch held at `pressed`; count events. */
typedef struct { int tap, hold, cancel, off; uint32_t t_hold, t_off; } evc_t;

static void run(pbtn_t *b, uint32_t *t, uint32_t ms, bool pressed, evc_t *c) {
    for (uint32_t i = 0; i < ms; i++, (*t)++) {
        switch (pbtn_update(b, pressed, *t)) {
        case PB_EV_TAP: c->tap++; break;
        case PB_EV_HOLD: c->hold++; c->t_hold = *t; break;
        case PB_EV_CANCEL: c->cancel++; break;
        case PB_EV_OFF: c->off++; c->t_off = *t; break;
        default: break;
        }
    }
}

static void test_pbtn(void) {
    printf("power: button tap / hold / cancel / off\n");
    pbtn_t b;
    uint32_t t = 1000;
    evc_t c;

    /* short taps of any length below the hold time: one tap each, on release */
    static const uint32_t taps[] = { 1, 10, 80, 300, 1000, 1999 };
    for (unsigned i = 0; i < sizeof taps / sizeof taps[0]; i++) {
        pbtn_init(&b, 2000, 2000, false);
        memset(&c, 0, sizeof c);
        run(&b, &t, taps[i], true, &c);
        CHECK(c.tap == 0 && c.hold == 0);          /* nothing while held */
        run(&b, &t, 50, false, &c);
        CHECK(c.tap == 1 && c.hold == 0 && c.cancel == 0 && c.off == 0);
    }
    /* exactly the hold time: hold, no tap */
    pbtn_init(&b, 2000, 2000, false);
    memset(&c, 0, sizeof c);
    uint32_t t0 = t;
    run(&b, &t, 2001, true, &c);
    CHECK(c.hold == 1 && c.tap == 0 && c.t_hold == t0 + 2000);
    CHECK(b.state == PB_HOLD);
    /* progress through the countdown */
    CHECK(pbtn_progress(&b, t0 + 2000) == 0);
    CHECK(pbtn_progress(&b, t0 + 3000) == 500);
    CHECK(pbtn_progress(&b, t0 + 3999) == 999);
    /* release during the countdown: cancel only - no tap, no select */
    run(&b, &t, 500, true, &c);
    run(&b, &t, 100, false, &c);
    CHECK(c.cancel == 1 && c.tap == 0 && c.off == 0 && b.state == PB_IDLE);
    CHECK(pbtn_progress(&b, t) == 0);
    /* the next short press is a normal tap again */
    run(&b, &t, 120, true, &c);
    run(&b, &t, 20, false, &c);
    CHECK(c.tap == 1);

    /* hold to the end: off at hold + countdown, release does nothing */
    pbtn_init(&b, 2000, 2000, false);
    memset(&c, 0, sizeof c);
    t0 = t;
    run(&b, &t, 6000, true, &c);
    CHECK(c.hold == 1 && c.off == 1 && c.t_off == t0 + 4000 && c.cancel == 0 && c.tap == 0);
    CHECK(pbtn_progress(&b, t) == 1000);
    run(&b, &t, 100, false, &c);
    CHECK(c.tap == 0 && c.cancel == 0 && c.off == 1 && b.state == PB_IDLE);

    /* pressed at init (the power-on press): ignored, however long, until released */
    for (uint32_t held = 5; held <= 9000; held *= 3) {
        pbtn_init(&b, 2000, 2000, true);
        memset(&c, 0, sizeof c);
        run(&b, &t, held, true, &c);
        run(&b, &t, 30, false, &c);
        CHECK(c.tap == 0 && c.hold == 0 && c.off == 0 && c.cancel == 0);
        run(&b, &t, 60, true, &c);
        run(&b, &t, 30, false, &c);
        CHECK(c.tap == 1);
    }

    /* timer wrap-around (32-bit ms counter) */
    pbtn_init(&b, 2000, 2000, false);
    memset(&c, 0, sizeof c);
    t = 0xFFFFFFFFu - 1000u;
    run(&b, &t, 2500, true, &c);
    CHECK(c.hold == 1 && c.off == 0);
    run(&b, &t, 1600, true, &c);
    CHECK(c.off == 1);

    /* a bouncy release after the debouncer still yields one event each */
    pbtn_init(&b, 2000, 2000, false);
    memset(&c, 0, sizeof c);
    for (int k = 0; k < 50; k++) { run(&b, &t, 40, true, &c); run(&b, &t, 40, false, &c); }
    CHECK(c.tap == 50 && c.hold == 0);

    /* other hold / countdown lengths */
    pbtn_init(&b, 1500, 3000, false);
    memset(&c, 0, sizeof c);
    t0 = t;
    run(&b, &t, 5000, true, &c);
    CHECK(c.t_hold == t0 + 1500 && c.t_off == t0 + 4500);
}

static void test_autooff(void) {
    printf("power: auto-off timing\n");
    autooff_t a;
    uint32_t s;
    CHECK(autooff_minutes(0) == 0 && autooff_minutes(1) == 15 && autooff_minutes(2) == 30);
    CHECK(autooff_minutes(3) == 60 && autooff_minutes(4) == 120 && autooff_minutes(5) == 0);

    for (unsigned idx = 1; idx < AUTOOFF_CHOICES; idx++) {
        uint32_t per = autooff_minutes(idx) * 60000u;
        uint32_t t0 = 5000;
        autooff_init(&a, t0);
        CHECK(autooff_eval(&a, t0, autooff_minutes(idx), true, &s) == AO_IDLE && s == 0);
        CHECK(autooff_eval(&a, t0 + per - 60001u, autooff_minutes(idx), true, &s) == AO_IDLE);
        CHECK(autooff_eval(&a, t0 + per - 60000u, autooff_minutes(idx), true, &s) == AO_WARN && s == 60);
        CHECK(autooff_eval(&a, t0 + per - 59001u, autooff_minutes(idx), true, &s) == AO_WARN && s == 60);
        CHECK(autooff_eval(&a, t0 + per - 59000u, autooff_minutes(idx), true, &s) == AO_WARN && s == 59);
        CHECK(autooff_eval(&a, t0 + per - 1u, autooff_minutes(idx), true, &s) == AO_WARN && s == 1);
        CHECK(autooff_eval(&a, t0 + per, autooff_minutes(idx), true, &s) == AO_FIRE);
        /* any activity during the warning cancels it and restarts the period */
        autooff_init(&a, t0);
        CHECK(autooff_eval(&a, t0 + per - 30000u, autooff_minutes(idx), true, NULL) == AO_WARN);
        autooff_activity(&a, t0 + per - 30000u);
        CHECK(autooff_eval(&a, t0 + per, autooff_minutes(idx), true, NULL) == AO_IDLE);
        CHECK(autooff_eval(&a, t0 + 2 * per - 30000u, autooff_minutes(idx), true, NULL) == AO_FIRE);
    }
    /* Off: never */
    autooff_init(&a, 0);
    CHECK(autooff_eval(&a, 400000000u, 0, true, NULL) == AO_IDLE);
    /* never on USB, and unplugging starts a full period */
    autooff_init(&a, 0);
    CHECK(autooff_eval(&a, 10u * 3600000u, 15, false, NULL) == AO_IDLE);
    uint32_t unplug = 10u * 3600000u + 1;
    CHECK(autooff_eval(&a, unplug, 15, false, NULL) == AO_IDLE);
    CHECK(autooff_eval(&a, unplug + 14u * 60000u - 1u, 15, true, NULL) == AO_IDLE);
    CHECK(autooff_eval(&a, unplug + 14u * 60000u, 15, true, NULL) == AO_WARN);
    CHECK(autooff_eval(&a, unplug + 15u * 60000u, 15, true, NULL) == AO_FIRE);
    /* USB plugged during the warning: back to idle */
    CHECK(autooff_eval(&a, unplug + 15u * 60000u + 5u, 15, false, NULL) == AO_IDLE);
    /* ms counter wrap */
    autooff_init(&a, 0xFFFFFFFFu - 100000u);
    CHECK(autooff_eval(&a, 0xFFFFFFFFu - 100000u + 15u * 60000u - 10000u, 15, true, &s) == AO_WARN && s == 10);
    CHECK(autooff_eval(&a, 0xFFFFFFFFu - 100000u + 15u * 60000u, 15, true, NULL) == AO_FIRE);
}

static void test_lowbatt(void) {
    printf("power: low battery thresholds\n");
    const lowbatt_cfg_t cfg = { 10, 5, 3, 3300, 3 };
    lowbatt_t l;
    lowbatt_out_t o;
    lowbatt_init(&l, &cfg);
    /* discharge 100 -> 0 on battery, cell voltage healthy */
    int popups = 0, first_warn = -1, first_popup = -1, shut_at = -1;
    for (int soc = 100; soc >= 0; soc--) {
        for (int k = 0; k < 3; k++) {
            o = lowbatt_eval(&l, true, (unsigned)soc, 3700, false);
            if (o.warn && first_warn < 0) first_warn = soc;
            if (o.popup) { popups++; if (first_popup < 0) first_popup = soc; }
            if (o.shutdown && shut_at < 0) shut_at = soc;
        }
    }
    CHECK(first_warn == 10 && first_popup == 5 && popups == 1 && shut_at == 3);
    /* shutdown needs 3 samples in a row */
    lowbatt_init(&l, &cfg);
    CHECK(!lowbatt_eval(&l, true, 3, 3700, false).shutdown);
    CHECK(!lowbatt_eval(&l, true, 3, 3700, false).shutdown);
    CHECK(!lowbatt_eval(&l, true, 4, 3700, false).shutdown);      /* back above: count restarts */
    CHECK(!lowbatt_eval(&l, true, 3, 3700, false).shutdown);
    CHECK(!lowbatt_eval(&l, true, 3, 3700, false).shutdown);
    CHECK(lowbatt_eval(&l, true, 3, 3700, false).shutdown);
    /* cell voltage alone (gauge still optimistic) */
    lowbatt_init(&l, &cfg);
    for (int k = 0; k < 2; k++) CHECK(!lowbatt_eval(&l, true, 40, 3299, false).shutdown);
    CHECK(lowbatt_eval(&l, true, 40, 3299, false).shutdown);
    lowbatt_init(&l, &cfg);
    for (int k = 0; k < 10; k++) CHECK(!lowbatt_eval(&l, true, 40, 3300, false).shutdown);
    /* never on USB: no warning, no popup, no shutdown */
    lowbatt_init(&l, &cfg);
    for (int k = 0; k < 10; k++) {
        o = lowbatt_eval(&l, true, 1, 3000, true);
        CHECK(!o.warn && !o.popup && !o.shutdown);
    }
    /* no gauge: nothing */
    lowbatt_init(&l, &cfg);
    for (int k = 0; k < 10; k++) {
        o = lowbatt_eval(&l, false, 0, 0, false);
        CHECK(!o.warn && !o.popup && !o.shutdown);
    }
    /* popup re-arms after charging (USB) or climbing back 2 % above its level */
    lowbatt_init(&l, &cfg);
    CHECK(lowbatt_eval(&l, true, 5, 3600, false).popup);
    CHECK(!lowbatt_eval(&l, true, 5, 3600, false).popup);
    CHECK(!lowbatt_eval(&l, true, 6, 3600, false).popup);
    CHECK(!lowbatt_eval(&l, true, 5, 3600, false).popup);         /* jitter 5-6: still once */
    lowbatt_eval(&l, true, 7, 3600, false);
    CHECK(lowbatt_eval(&l, true, 5, 3600, false).popup);          /* re-armed at 7 % */
    lowbatt_eval(&l, true, 5, 3600, true);                        /* USB */
    CHECK(lowbatt_eval(&l, true, 5, 3600, false).popup);
    /* jumping straight to 2 %: popup and (after 3 samples) shutdown */
    lowbatt_init(&l, &cfg);
    o = lowbatt_eval(&l, true, 2, 3500, false);
    CHECK(o.warn && o.popup && !o.shutdown);
    /* icon levels */
    CHECK(batt_level(0) == 0 && batt_level(10) == 0 && batt_level(11) == 1 && batt_level(35) == 1);
    CHECK(batt_level(36) == 2 && batt_level(60) == 2 && batt_level(61) == 3 && batt_level(85) == 3);
    CHECK(batt_level(86) == 4 && batt_level(100) == 4);
}

static void test_gauge_conv(void) {
    printf("power: MAX17048 conversions\n");
    CHECK(max17048_vcell_mv(0) == 0);
    CHECK(max17048_vcell_mv(53248) == 4160);         /* 0xD000 * 78.125 uV */
    CHECK(max17048_vcell_mv(42240) == 3300);         /* 3.300 V */
    CHECK(max17048_vcell_mv(0xFFFF) == 5120);        /* full scale 5.12 V */
    CHECK(max17048_vcell_mv(1) == 0 && max17048_vcell_mv(7) == 1 && max17048_vcell_mv(13) == 1);
    for (unsigned mv = 2500; mv <= 4500; mv += 7) {
        uint16_t raw = (uint16_t)(mv * 64u / 5u);
        CHECK(max17048_vcell_mv(raw) == mv);
    }
    CHECK(max17048_soc_pct(0x0000) == 0);
    CHECK(max17048_soc_pct(0x4C80) == 76);           /* 76.5 % */
    CHECK(max17048_soc_x10(0x4C80) == 765);
    CHECK(max17048_soc_pct(0x64FF) == 100);
    CHECK(max17048_soc_pct(0x6600) == 100);          /* 102 % at end of charge: clamped */
    CHECK(max17048_soc_x10(0x6600) == 1000);
    CHECK(max17048_soc_x10(0x0119) == 11);           /* 1.098 % -> 1.1 */
    CHECK(max17048_soc_pct(0x02FF) == 2);            /* 2.996 %: whole percent is 2 */
    CHECK(max17048_crate_x1000(0) == 0);
    CHECK(max17048_crate_x1000(1) == 208);
    CHECK(max17048_crate_x1000(0xFFFF) == -208);     /* signed */
    CHECK(max17048_crate_x1000(100) == 20800);       /* +20.8 %/h */
    CHECK(max17048_crate_x1000(0x8000) == -32768 * 208);
    CHECK(max17048_athd(4) == 0x1C && max17048_athd(5) == 27 && max17048_athd(1) == 31 && max17048_athd(32) == 0);
    CHECK(max17048_athd(0) == 31 && max17048_athd(40) == 0);
    /* CONFIG: default 0x971C -> 5 %: RCOMP kept, ATHD 27 */
    CHECK(max17048_config_with_athd(0x971C, 5) == 0x971B);
    /* ALRT cleared, SLEEP and ALSC kept */
    CHECK(max17048_config_with_athd(0x97FC, 5) == 0x97DB);
    CHECK(max17048_config_with_athd(0x1234 | 0x20, 4) == 0x121C);
    CHECK(max17048_version_ok(0x0010) && max17048_version_ok(0x0012) && max17048_version_ok(0x001F));
    CHECK(!max17048_version_ok(0x0000) && !max17048_version_ok(0xFFFF) && !max17048_version_ok(0x0020));
}

/* ---- log close record ---- */
static void drain_all(log_t *L) {
    for (int i = 0; i < 10000 && log_pending(L); i++) log_service(L, true);
}

static void put_msgs(log_t *L, int n, uint64_t *t) {
    for (int i = 0; i < n; i++) {
        midi_msg_t m;
        memset(&m, 0, sizeof m);
        m.type = MT_NOTE_ON; m.status = 0x90; m.d1 = (uint8_t)(i & 127); m.d2 = 100;
        *t += 1000 + (uint64_t)(i % 7) * 333;
        m.t_us = *t;
        log_time_t lt = { *t, false, 0, 0 };
        log_append(L, &m, &lt);
    }
}

static void test_log_close(void) {
    printf("power: log session close record\n");
    const uint32_t size = 16 * LOG_SECTOR_SIZE;
    uint8_t *mem = malloc(size);
    log_ram_flash_t rf;
    log_flash_t fl;
    log_ram_flash_init(&rf, &fl, mem, size, true);
    static log_t L, L2;
    log_init(&L, &fl);
    CHECK(!L.prev_known && L.prev_end == LOG_END_NONE);

    /* boot 1: messages, clean shutdown */
    uint64_t t = 100;
    log_time_t st = { t, false, 0, 0 };
    log_new_session(&L, &st);
    log_commit_start(&L);
    uint32_t s1 = log_session(&L, 0)->no;
    put_msgs(&L, 500, &t);
    log_time_t end = { t + 5000, true, 843000000u, 250 };
    CHECK(log_close_session(&L, &end, LOG_END_CLEAN));
    CHECK(!L.live && log_find(&L, s1)->closed && log_find(&L, s1)->end_reason == LOG_END_CLEAN);
    CHECK(!log_close_session(&L, &end, LOG_END_CLEAN));          /* not live any more */
    midi_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = MT_CLOCK; m.status = 0xF8;
    CHECK(!log_append(&L, &m, &end));                            /* nothing logged after close */
    drain_all(&L);

    /* boot 2: previous session closed cleanly, all 500 messages readable */
    log_init(&L2, &fl);
    CHECK(L2.prev_known && L2.prev_end == LOG_END_CLEAN && L2.bad_pages == 0);
    const log_sess_t *e = log_find(&L2, s1);
    CHECK(e && e->closed && e->nrec == 500 && e->has_end && e->end.t_us == t + 5000 && e->end.real && e->end.real_ms == 250);
    int n = 0;
    log_pos_t p;
    bool ok = log_pos_first(&L2, s1, &p);
    while (ok) { n++; ok = log_pos_next(&L2, s1, &p); }
    CHECK(n == 500);
    /* boot 2 session: committed start page only, then power is lost */
    st.t_us = 10;
    log_new_session(&L2, &st);
    log_commit_start(&L2);
    uint32_t s2 = log_session(&L2, 0)->no;
    drain_all(&L2);
    log_init(&L, &fl);
    CHECK(L.prev_known && L.prev_end == LOG_END_NONE);           /* power loss */
    CHECK(log_find(&L, s2) && log_find(&L, s2)->nrec == 0 && !log_find(&L, s2)->closed);
    CHECK(log_find(&L, s1)->closed);

    /* boot 3: messages, part page in RAM, power cut (no close): power loss */
    log_new_session(&L, &st);
    log_commit_start(&L);
    put_msgs(&L, 40, &t);
    log_flush(&L);
    drain_all(&L);
    put_msgs(&L, 3, &t);                                        /* lost with the RAM page */
    log_init(&L2, &fl);
    CHECK(L2.prev_end == LOG_END_NONE);

    /* boot 4: empty session (no commit), closed: start page + close page */
    st.t_us = 77;
    log_new_session(&L2, &st);
    uint32_t s4 = log_session(&L2, 0)->no;
    log_time_t e4 = { 9000000, false, 0, 0 };
    CHECK(log_close_session(&L2, &e4, LOG_END_AUTOOFF));
    drain_all(&L2);
    log_init(&L, &fl);
    CHECK(L.prev_end == LOG_END_AUTOOFF);
    e = log_find(&L, s4);
    CHECK(e && e->closed && !e->partial && e->nrec == 0 && e->start.t_us == 77 && e->end.t_us == 9000000);
    CHECK(!log_pos_first(&L, s4, &p));                           /* nothing to read */

    /* low battery reason; close page with a full RAM queue is refused */
    log_new_session(&L, &st);
    put_msgs(&L, 20, &t);
    log_time_t e5 = { t + 1, false, 0, 0 };
    CHECK(log_close_session(&L, &e5, LOG_END_LOWBATT));
    drain_all(&L);
    log_init(&L2, &fl);
    CHECK(L2.prev_end == LOG_END_LOWBATT);

    /* ring wrap: many closed sessions; close pages are dropped with their
     * sector and never confuse the reader or the index */
    for (int k = 0; k < 120; k++) {
        log_time_t s0 = { t, false, 0, 0 };
        log_new_session(&L2, &s0);
        log_commit_start(&L2);
        put_msgs(&L2, 60 + (k * 37) % 300, &t);
        for (int i = 0; i < 4; i++) log_service(&L2, true);
        log_time_t ee = { t + 10, false, 0, 0 };
        CHECK(log_close_session(&L2, &ee, (uint8_t)(k % 3 == 0 ? LOG_END_AUTOOFF : LOG_END_CLEAN)));
        drain_all(&L2);
    }
    CHECK(L2.drops == 0);
    log_init(&L, &fl);
    CHECK(L.bad_pages == 0 && L.prev_end == LOG_END_CLEAN);     /* k = 119: 119 % 3 != 0 */
    int bad = 0;
    for (int i = 0; i < log_session_count(&L); i++) {
        const log_sess_t *se = log_session(&L, i);
        int cnt = 0;
        ok = log_pos_first(&L, se->no, &p);
        while (ok) { cnt++; ok = log_pos_next(&L, se->no, &p); }
        if ((uint32_t)cnt != se->nrec) bad++;
        if (!se->partial && !se->closed) bad++;                  /* every whole session here was closed */
        if (se->closed && !se->has_end) bad++;
    }
    CHECK(bad == 0);
    CHECK(log_session_count(&L) > 3 && log_session_count(&L) < 120);
    /* the index after wrapping matches the writer's (same sessions) */
    CHECK(log_session_count(&L) == log_session_count(&L2));

    /* torn close page (power lost while programming it): power loss, page skipped */
    log_new_session(&L, &st);
    log_commit_start(&L);
    put_msgs(&L, 10, &t);
    log_flush(&L);
    drain_all(&L);
    rf.fail_after_programs = 0;
    rf.fail_bytes = 40;
    log_time_t e6 = { t + 1, false, 0, 0 };
    CHECK(log_close_session(&L, &e6, LOG_END_CLEAN));
    drain_all(&L);
    log_ram_flash_init(&rf, &fl, mem, size, false);              /* power back */
    log_init(&L2, &fl);
    CHECK(L2.prev_end == LOG_END_NONE && L2.bad_pages == 1);
    free(mem);
}

void test_power(void) {
    test_pbtn();
    test_autooff();
    test_lowbatt();
    test_gauge_conv();
    test_log_close();
}
