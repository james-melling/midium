/*
 * power.c - soft power and battery on core0. See power.h; the decisions
 * themselves are in power_fsm.c (unit tested).
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "config.h"
#include "power.h"
#include "power_fsm.h"
#include "max17048.h"
#include "settings.h"
#include "midi_io.h"
#include "generator.h"
#include "logger.h"
#include "display.h"

#define VBUS_POLL_US     100000u
#define SHUTDOWN_SHOW_US 300000u    /* "Shutting down" on screen before the work */
#define OFF_SHOW_US      800000u    /* "Off / Safe to unplug" before the kill line */
#define IDLE_DISPLAY_US  3000000u   /* soft-off: panel off after this */
#define CHARGE_DISPLAY_US 10000000u /* charge-only screen: panel off after this */
#define WAKE_DEBOUNCE_US 30000u

static pwr_state_t s_state;
static uint8_t     s_reason;
static uint64_t    s_t;             /* entered the current state */
static bool        s_centre_at_boot;

static batt_info_t s_batt;
static uint64_t    s_next_poll, s_next_vbus;
static lowbatt_t   s_lb;
static bool        s_warn, s_popup_due;
static autooff_t   s_ao;
static uint32_t    s_last_rx;
static bool        s_disp_off;

/* soft-off wake: release, then press */
static bool        s_wake_armed;
static uint64_t    s_wake_t;

static inline uint32_t ms_of(uint64_t us) { return (uint32_t)(us / 1000u); }

static void kill_line(bool kill) {
    bool level = PWR_KILL_ACTIVE_LOW ? !kill : kill;
    gpio_put(PIN_PWR_KILL, level);
}

void power_early_init(void) {
    gpio_init(PIN_PWR_KILL);
    kill_line(false);                       /* hold the power on */
    gpio_set_dir(PIN_PWR_KILL, GPIO_OUT);
#if PIN_VBUS_SENSE >= 0
    gpio_init(PIN_VBUS_SENSE);
    gpio_set_dir(PIN_VBUS_SENSE, GPIO_IN);
    gpio_disable_pulls(PIN_VBUS_SENSE);     /* the board divider drives it */
#endif
#if PIN_CHG_STAT >= 0
    gpio_init(PIN_CHG_STAT);
    gpio_set_dir(PIN_CHG_STAT, GPIO_IN);
    gpio_pull_up(PIN_CHG_STAT);             /* open-drain status */
#endif
    gpio_init(PIN_JOY_PRESS);
    gpio_set_dir(PIN_JOY_PRESS, GPIO_IN);
    gpio_pull_up(PIN_JOY_PRESS);
    sleep_us(50);
    s_centre_at_boot = !gpio_get(PIN_JOY_PRESS);
    s_state = PWR_RUN;
}

static bool read_usb(void) {
#if PIN_VBUS_SENSE >= 0
    return gpio_get(PIN_VBUS_SENSE);
#else
    return false;
#endif
}

static void poll_gauge(void) {
    max17048_sample_t smp;
    if (!s_batt.present || !max17048_read(&smp)) return;
    s_batt.mv = max17048_vcell_mv(smp.vcell);
    s_batt.soc_pct = max17048_soc_pct(smp.soc);
    s_batt.soc_x10 = max17048_soc_x10(smp.soc);
    s_batt.crate_x1000 = max17048_crate_x1000(smp.crate);
}

static void update_charging(void) {
#if PIN_CHG_STAT >= 0
    s_batt.charging = s_batt.usb && !gpio_get(PIN_CHG_STAT);
#else
    s_batt.charging = s_batt.present && s_batt.usb && s_batt.soc_pct < 100;
#endif
}

bool power_init(void) {
    static const lowbatt_cfg_t cfg = { BATT_WARN_PCT, BATT_POPUP_PCT, BATT_OFF_PCT, BATT_OFF_MV, BATT_OFF_SAMPLES };
    uint64_t now = time_us_64();
    lowbatt_init(&s_lb, &cfg);
    autooff_init(&s_ao, ms_of(now));
    s_last_rx = midi_io_last_rx_ms();
    s_batt.present = max17048_init(BATT_POPUP_PCT);
    s_batt.usb = read_usb();
    poll_gauge();
    update_charging();
    s_next_poll = now + BATT_POLL_MS * 1000u;
    s_next_vbus = now + VBUS_POLL_US;
#if MIDIUM_PWR_CTRL
    /* Powered up by USB (VBUS -> controller ON strap), not by the button,
     * not a wake-up from soft-off: charge only, if that is the setting. */
    if (s_batt.usb && !s_centre_at_boot && !midi_io_soft_wake() && g_settings.usb_power_on == USBPO_CHARGE) {
        midi_io_shutdown();
        s_state = PWR_CHARGE_ONLY;
        s_t = now;
        s_wake_armed = true;
        s_wake_t = 0;
        return true;
    }
#endif
    return false;
}

void power_activity(void) { autooff_activity(&s_ao, ms_of(time_us_64())); }

void power_request_off(uint8_t reason) {
    if (s_state != PWR_RUN) return;
    s_reason = reason;
    s_state = PWR_SHUTDOWN;
    s_t = time_us_64();
}

static void stop_generator(void) {
    if (gen_clock_running()) gen_clock_toggle();
    if (gen_note_sweep_active()) gen_note_sweep_toggle();
    if (gen_pb_sweep_active()) gen_pb_sweep_toggle();
}

static void display_off(void) {
    const display_driver_t *d = con_driver();
    if (d && d->power) d->power(false);
    s_disp_off = true;
}

/* Soft-off: a centre press (after a release) reboots. */
static void wake_poll(uint64_t now) {
    bool down = !gpio_get(PIN_JOY_PRESS);
    if (!down) { s_wake_armed = true; s_wake_t = 0; return; }
    if (!s_wake_armed) return;
    if (!s_wake_t) { s_wake_t = now; return; }
    if (now - s_wake_t >= WAKE_DEBOUNCE_US) midi_io_reboot();
}

static void run_task(uint64_t now) {
    uint32_t ms = ms_of(now);
    if (now >= s_next_vbus) {
        s_next_vbus = now + VBUS_POLL_US;
        s_batt.usb = read_usb();
        update_charging();
    }
    if (now >= s_next_poll) {
        s_next_poll = now + BATT_POLL_MS * 1000u;
        poll_gauge();
        update_charging();
        lowbatt_out_t lo = lowbatt_eval(&s_lb, s_batt.present, s_batt.soc_pct, s_batt.mv, s_batt.usb);
        s_warn = lo.warn;
        if (lo.popup) s_popup_due = true;
        if (lo.shutdown) { power_request_off(LOG_END_LOWBATT); return; }
    }
    /* activity: MIDI in, a running generator (keys: power_activity) */
    uint32_t rx = midi_io_last_rx_ms();
    if (rx != s_last_rx) { s_last_rx = rx; autooff_activity(&s_ao, ms); }
    if (gen_clock_running() || gen_note_sweep_active() || gen_pb_sweep_active()) autooff_activity(&s_ao, ms);
    bool on_batt = s_batt.present && !s_batt.usb;
    if (autooff_eval(&s_ao, ms, power_autooff_minutes(), on_batt, NULL) == AO_FIRE)
        power_request_off(LOG_END_AUTOOFF);
}

void power_task(uint64_t now) {
    switch (s_state) {
    case PWR_RUN:
        run_task(now);
        break;
    case PWR_SHUTDOWN:
        if (now - s_t < SHUTDOWN_SHOW_US) break;     /* the screen says so first */
        stop_generator();
        logger_shutdown(s_reason);
        midi_io_shutdown();
        s_state = PWR_OFF_MSG;
        s_t = time_us_64();
        break;
    case PWR_OFF_MSG:
        if (now - s_t < OFF_SHOW_US) break;
        kill_line(true);                            /* the controller cuts the power */
        s_state = PWR_KILLED;
        s_t = now;
        break;
    case PWR_KILLED:
        if (now - s_t < PWR_KILL_WAIT_MS * 1000u) break;
        /* still here: no controller (or USB keeps it alive) */
        s_state = PWR_IDLE;
        s_t = now;
        s_wake_armed = false;
        s_wake_t = 0;
        break;
    case PWR_IDLE:
        if (!s_disp_off && now - s_t >= IDLE_DISPLAY_US) display_off();
        wake_poll(now);
        break;
    case PWR_CHARGE_ONLY:
        if (now >= s_next_poll) {
            s_next_poll = now + BATT_POLL_MS * 1000u;
            s_batt.usb = read_usb();
            poll_gauge();
            update_charging();
            if (!s_batt.usb) { kill_line(true); s_state = PWR_KILLED; s_t = now; break; }   /* unplugged */
        }
        if (!s_disp_off && now - s_t >= CHARGE_DISPLAY_US) display_off();
        wake_poll(now);
        break;
    }
}

pwr_state_t power_state(void) { return s_state; }
uint8_t power_off_reason(void) { return s_reason; }
const batt_info_t *power_batt(void) { return &s_batt; }
bool power_batt_warn(void) { return s_batt.present && !s_batt.usb && s_warn; }
bool power_display_off(void) { return s_disp_off; }

bool power_take_lowbatt_popup(void) {
    bool p = s_popup_due;
    s_popup_due = false;
    return p;
}

uint16_t power_autooff_minutes(void) { return autooff_minutes(g_settings.auto_off_idx); }

bool power_autooff_warning(uint32_t *secs_left) {
    if (s_state != PWR_RUN) return false;
    bool on_batt = s_batt.present && !s_batt.usb;
    uint32_t left = 0;
    ao_state_t st = autooff_eval(&s_ao, ms_of(time_us_64()), power_autooff_minutes(), on_batt, &left);
    if (secs_left) *secs_left = left;
    return st == AO_WARN;
}

uint32_t power_autooff_secs_left(void) {
    uint16_t m = power_autooff_minutes();
    if (s_state != PWR_RUN || !m || !s_batt.present || s_batt.usb) return 0;
    uint32_t idle = ms_of(time_us_64()) - s_ao.last_ms, period = (uint32_t)m * 60000u;
    return idle >= period ? 0u : (period - idle + 999u) / 1000u;
}

const char *power_last_off_text(void) {
    uint8_t r;
    if (!logger_prev_end(&r)) return "unknown";
    switch (r) {
    case LOG_END_CLEAN:   return "clean";
    case LOG_END_LOWBATT: return "low battery";
    case LOG_END_AUTOOFF: return "auto-off";
    default:              return midi_io_watchdog_reset() ? "watchdog" : "power loss";
    }
}

#if MIDIUM_SIM
void power_sim_idle_skip(uint32_t ms) { s_ao.last_ms -= ms; }
#endif
