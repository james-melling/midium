/*
 * joystick.h - 5-way joystick: 1 ms sampling timer, debounce, auto-repeat.
 *
 * The centre switch is also the power button (power_fsm.h): JOY_PRESS is
 * sent when a short press is RELEASED, so a press that becomes a long hold
 * never also selects anything. Holding it PWR_HOLD_MS sends JOY_HOLD (the
 * power-off countdown starts), releasing during the countdown sends
 * JOY_HOLD_CANCEL, holding on for PWR_COUNTDOWN_MS more sends JOY_HOLD_OFF.
 * A centre press already held when the joystick starts (the press that
 * powered the box on) is ignored until released.
 */
#ifndef MIDIUM_JOYSTICK_H
#define MIDIUM_JOYSTICK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT,
    JOY_PRESS,          /* centre tapped (sent on release) */
    JOY_HOLD,           /* centre held PWR_HOLD_MS: power-off countdown */
    JOY_HOLD_CANCEL,    /* released during the countdown */
    JOY_HOLD_OFF,       /* held to the end of the countdown: power off */
} joy_event_t;

void joystick_init(void);
/* Next queued event (press or auto-repeat), JOY_NONE if none. */
joy_event_t joystick_get(void);

#endif /* MIDIUM_JOYSTICK_H */
