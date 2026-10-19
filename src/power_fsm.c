/*
 * power_fsm.c - power button, auto-off and low-battery logic. See power_fsm.h.
 */
#include "power_fsm.h"

/* ---- power button ---- */
void pbtn_init(pbtn_t *b, uint32_t hold_ms, uint32_t count_ms, bool pressed_now) {
    b->state = pressed_now ? PB_IGNORE : PB_IDLE;
    b->down_ms = 0;
    b->hold_ms = hold_ms;
    b->count_ms = count_ms;
}

pb_event_t pbtn_update(pbtn_t *b, bool pressed, uint32_t now) {
    uint32_t held = now - b->down_ms;
    switch ((pb_state_t)b->state) {
    case PB_IDLE:
        if (pressed) { b->state = PB_DOWN; b->down_ms = now; }
        return PB_EV_NONE;
    case PB_DOWN:
        if (!pressed) { b->state = PB_IDLE; return PB_EV_TAP; }
        if (held >= b->hold_ms) { b->state = PB_HOLD; return PB_EV_HOLD; }
        return PB_EV_NONE;
    case PB_HOLD:
        if (!pressed) { b->state = PB_IDLE; return PB_EV_CANCEL; }
        if (held >= b->hold_ms + b->count_ms) { b->state = PB_FIRED; return PB_EV_OFF; }
        return PB_EV_NONE;
    case PB_FIRED:
    case PB_IGNORE:
    default:
        if (!pressed) b->state = PB_IDLE;
        return PB_EV_NONE;
    }
}

unsigned pbtn_progress(const pbtn_t *b, uint32_t now) {
    if (b->state == PB_FIRED) return 1000u;
    if (b->state != PB_HOLD || b->count_ms == 0) return 0;
    uint32_t in = now - b->down_ms - b->hold_ms;
    if (in >= b->count_ms) return 1000u;
    return (unsigned)((uint64_t)in * 1000u / b->count_ms);
}

/* ---- auto-off ---- */
void autooff_init(autooff_t *a, uint32_t now) { a->last_ms = now; }
void autooff_activity(autooff_t *a, uint32_t now) { a->last_ms = now; }

ao_state_t autooff_eval(autooff_t *a, uint32_t now, uint16_t minutes, bool on_battery, uint32_t *secs_left) {
    if (secs_left) *secs_left = 0;
    if (!minutes || !on_battery) { a->last_ms = now; return AO_IDLE; }
    uint32_t period = (uint32_t)minutes * 60000u;
    uint32_t idle = now - a->last_ms;
    if (idle >= period) return AO_FIRE;
    uint32_t left = period - idle;
    if (left > AUTOOFF_WARN_MS) return AO_IDLE;
    if (secs_left) *secs_left = (left + 999u) / 1000u;
    return AO_WARN;
}

uint16_t autooff_minutes(unsigned idx) {
    static const uint16_t m[AUTOOFF_CHOICES] = { 0, 15, 30, 60, 120 };
    return idx < AUTOOFF_CHOICES ? m[idx] : 0;
}

/* ---- low battery ---- */
void lowbatt_init(lowbatt_t *l, const lowbatt_cfg_t *cfg) {
    l->cfg = *cfg;
    l->popup_done = false;
    l->low_count = 0;
}

lowbatt_out_t lowbatt_eval(lowbatt_t *l, bool gauge_ok, unsigned soc, unsigned mv, bool usb) {
    lowbatt_out_t o = { false, false, false };
    if (!gauge_ok || usb) {
        l->low_count = 0;
        if (usb) l->popup_done = false;
        return o;
    }
    if (soc >= (unsigned)l->cfg.popup_pct + 2u) l->popup_done = false;
    o.warn = soc <= l->cfg.warn_pct;
    if (soc <= l->cfg.popup_pct && !l->popup_done) {
        o.popup = true;
        l->popup_done = true;
    }
    if (soc <= l->cfg.off_pct || mv < l->cfg.off_mv) {
        if (l->low_count < 255) l->low_count++;
        if (l->low_count >= l->cfg.off_samples) o.shutdown = true;
    } else {
        l->low_count = 0;
    }
    return o;
}

unsigned batt_level(unsigned soc) {
    if (soc <= 10) return 0;
    if (soc <= 35) return 1;
    if (soc <= 60) return 2;
    if (soc <= 85) return 3;
    return 4;
}
