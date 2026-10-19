/*
 * power_fsm.h - the decision logic behind soft power and the battery
 * (portable C, unit tested on the host; no hardware access):
 *
 *   pbtn_*      the joystick centre switch, which is also the power button:
 *               tap / long hold / cancel / power off
 *   autooff_*   auto power-off on battery after a period of no activity
 *   lowbatt_*   low-battery warning, one-time popup and forced shutdown
 *
 * Gauge register conversions are in max17048.h.
 */
#ifndef MIDIUM_POWER_FSM_H
#define MIDIUM_POWER_FSM_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------------ */
/* Centre switch / power button                                              */
/* ------------------------------------------------------------------------ */
/* Fed with the debounced switch state once per sample (1 ms on the device).
 * A short press acts on RELEASE (PB_EV_TAP), so a press that turns into a
 * long hold never also counts as a select:
 *
 *   press ... release before hold_ms                -> PB_EV_TAP
 *   press ... held hold_ms                          -> PB_EV_HOLD (countdown starts)
 *         ... released before hold_ms + count_ms    -> PB_EV_CANCEL (nothing else)
 *         ... held hold_ms + count_ms               -> PB_EV_OFF (release: nothing)
 *
 * A switch already pressed at init (the press that powered the box on, or
 * woke it from soft-off) is ignored until it is released: it gives no tap
 * and no hold. */
typedef enum {
    PB_EV_NONE = 0,
    PB_EV_TAP,
    PB_EV_HOLD,
    PB_EV_CANCEL,
    PB_EV_OFF,
} pb_event_t;

typedef enum { PB_IDLE = 0, PB_DOWN, PB_HOLD, PB_FIRED, PB_IGNORE } pb_state_t;

typedef struct {
    uint8_t  state;          /* pb_state_t */
    uint32_t down_ms;        /* when the current press started */
    uint32_t hold_ms, count_ms;
} pbtn_t;

void       pbtn_init(pbtn_t *b, uint32_t hold_ms, uint32_t count_ms, bool pressed_now);
pb_event_t pbtn_update(pbtn_t *b, bool pressed, uint32_t now_ms);
/* Countdown progress 0..1000 while in PB_HOLD (1000 once fired), else 0. */
unsigned   pbtn_progress(const pbtn_t *b, uint32_t now_ms);

/* ------------------------------------------------------------------------ */
/* Auto-off                                                                  */
/* ------------------------------------------------------------------------ */
/* Times are ms on a free-running 32-bit counter (differences wrap safely).
 * Runs only on battery with a non-zero period. On USB power (or with the
 * period Off) the timer restarts continuously, so unplugging starts a full
 * period. Warning for the last AUTOOFF_WARN_MS. */
#define AUTOOFF_WARN_MS 60000u

typedef enum { AO_IDLE = 0, AO_WARN, AO_FIRE } ao_state_t;

typedef struct {
    uint32_t last_ms;        /* last activity */
} autooff_t;

void       autooff_init(autooff_t *a, uint32_t now_ms);
void       autooff_activity(autooff_t *a, uint32_t now_ms);
/* minutes = 0: off. secs_left (may be NULL): whole seconds to power-off,
 * rounded up, while AO_WARN. */
ao_state_t autooff_eval(autooff_t *a, uint32_t now_ms, uint16_t minutes, bool on_battery, uint32_t *secs_left);

/* Settings index <-> minutes: 0 Off, 15, 30, 60, 120. */
#define AUTOOFF_CHOICES 5
uint16_t   autooff_minutes(unsigned idx);

/* ------------------------------------------------------------------------ */
/* Low battery                                                               */
/* ------------------------------------------------------------------------ */
typedef struct {
    uint8_t  warn_pct;       /* header warning at or below (10) */
    uint8_t  popup_pct;      /* one-time popup at or below (5) */
    uint8_t  off_pct;        /* clean shutdown at or below (3) */
    uint16_t off_mv;         /* ... or cell below this (3300 mV) */
    uint8_t  off_samples;    /* consecutive samples needed for the shutdown */
} lowbatt_cfg_t;

typedef struct {
    lowbatt_cfg_t cfg;
    bool    popup_done;      /* popup shown this discharge */
    uint8_t low_count;       /* consecutive samples at the shutdown level */
} lowbatt_t;

typedef struct {
    bool warn;               /* show the header warning */
    bool popup;              /* show the popup now (once) */
    bool shutdown;           /* shut down now */
} lowbatt_out_t;

void lowbatt_init(lowbatt_t *l, const lowbatt_cfg_t *cfg);
/* One gauge sample. soc_pct = whole percent (0..100), mv = cell voltage.
 * Nothing happens without a gauge or while USB is powering the box; USB
 * also re-arms the popup (as does climbing back 2 % above its level). */
lowbatt_out_t lowbatt_eval(lowbatt_t *l, bool gauge_ok, unsigned soc_pct, unsigned mv, bool usb);

/* Battery icon level 0..4 for a percentage (0 = empty outline). */
unsigned batt_level(unsigned soc_pct);

#endif /* MIDIUM_POWER_FSM_H */
