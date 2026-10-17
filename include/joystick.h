/*
 * joystick.h - 5-way joystick: 1 ms sampling timer, debounce, auto-repeat.
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
    JOY_PRESS,
} joy_event_t;

void joystick_init(void);
/* Next queued event (press or auto-repeat), JOY_NONE if none. */
joy_event_t joystick_get(void);

#endif /* MIDIUM_JOYSTICK_H */
