/*
 * power.h - soft power and battery (core0).
 *
 * Hardware (final PCB): a push-button on/off controller (LTC2955 or
 * MAX16150 class) switches the system supply. The joystick centre switch
 * goes to both the MCU (GP22) and the controller's PB input (diode-OR), so
 * a press while off powers the box on. The MCU holds PIN_PWR_KILL inactive
 * while running and drives it active to power off; the controller also
 * forces power off after a long hold (~8 s) on its own. On the breadboard
 * nothing is connected to PIN_PWR_KILL, so after a shutdown the firmware
 * finds itself still running and goes to a low-power soft-off idle instead
 * (display off, relays released, core1 stopped) until the next press.
 *
 * Shutdown sequence (button hold, Menu > Power off, low battery, auto-off):
 *   "Shutting down" -> generator stopped, log session closed and written
 *   (logger_shutdown) -> core1 stopped, relays dropped (IN A -> OUT A
 *   passive) -> "Off / Safe to unplug" -> PIN_PWR_KILL -> (still running)
 *   soft-off idle -> a centre press reboots.
 *
 * Battery: a MAX17048 gauge on I2C1 (optional: no gauge, no battery UI),
 * USB power from PIN_VBUS_SENSE (GP24 on the Pico 2), polled once a second.
 */
#ifndef MIDIUM_POWER_H
#define MIDIUM_POWER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PWR_RUN = 0,
    PWR_SHUTDOWN,       /* "Shutting down": stopping, writing the log */
    PWR_OFF_MSG,        /* "Off / Safe to unplug", about to drive PIN_PWR_KILL */
    PWR_KILLED,         /* PIN_PWR_KILL driven, waiting for the power to go */
    PWR_IDLE,           /* soft-off idle (no controller): press to reboot */
    PWR_CHARGE_ONLY,    /* powered up by USB with "USB power-on: Charge only" */
} pwr_state_t;

typedef struct {
    bool     present;   /* gauge answered */
    unsigned soc_pct;   /* 0..100 */
    unsigned soc_x10;   /* tenths of a percent */
    unsigned mv;        /* cell voltage */
    int32_t  crate_x1000;  /* %/h x 1000, + charging */
    bool     usb;       /* USB power present (PIN_VBUS_SENSE) */
    bool     charging;
} batt_info_t;

/* First thing at boot: hold the power on, note whether the centre switch
 * is pressed (the press that powered the box on). */
void power_early_init(void);
/* After the display, I2C bus and clock: probe the gauge. Returns true if
 * this power-up should only charge (MIDIUM_PWR_CTRL builds: USB caused it
 * and the setting is "Charge only"); the box then shows the charging screen
 * and waits for a press, and the caller should not start logging. */
bool power_init(void);
void power_task(uint64_t now_us);

/* A key was used (auto-off timer; also cancels the auto-off warning). */
void power_activity(void);
/* Start the clean shutdown. reason: LOG_END_CLEAN / _LOWBATT / _AUTOOFF. */
void power_request_off(uint8_t reason);

pwr_state_t        power_state(void);
uint8_t            power_off_reason(void);
const batt_info_t *power_batt(void);
bool power_batt_warn(void);               /* at or below BATT_WARN_PCT on battery */
bool power_take_lowbatt_popup(void);      /* true once when the popup is due */
bool power_autooff_warning(uint32_t *secs_left);
uint16_t power_autooff_minutes(void);
/* Seconds until auto-off (0 = not counting: off, on USB, no gauge). */
uint32_t power_autooff_secs_left(void);
/* How the previous power-down went: "clean", "low battery", "auto-off",
 * "power loss", "watchdog", "unknown". */
const char *power_last_off_text(void);
/* Soft-off idle with the panel switched off. */
bool power_display_off(void);

#if MIDIUM_SIM
/* Simulator test hook: pretend the last activity was ms earlier. */
void power_sim_idle_skip(uint32_t ms);
#endif

#endif /* MIDIUM_POWER_H */
