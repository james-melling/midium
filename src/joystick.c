/*
 * joystick.c - sampled every 1 ms from a repeating timer (core0 IRQ).
 * A button changes state only after it has read the same for
 * JOY_DEBOUNCE_MS consecutive samples. UP/DOWN auto-repeat while held.
 * Events go to core0's main loop through an SPSC queue (IRQ -> thread).
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "config.h"
#include "joystick.h"
#include "spsc.h"

#define NBTN 5
static const uint8_t s_pins[NBTN] = { PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT, PIN_JOY_PRESS };
static const joy_event_t s_evt[NBTN] = { JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_PRESS };

typedef struct {
    bool     stable;     /* debounced pressed state */
    uint8_t  count;      /* consecutive samples differing from stable */
    uint32_t held_ms;
    uint32_t next_rep;
} btn_t;

static btn_t s_btn[NBTN];
static repeating_timer_t s_timer;

#define EVQ 16u
static spsc_t  s_q;
static uint8_t s_qbuf[EVQ];

static void push(joy_event_t e) {
    if (spsc_free(&s_q, EVQ) == 0) return;
    s_qbuf[spsc_head_slot(&s_q, EVQ)] = (uint8_t)e;
    spsc_push_commit(&s_q);
}

static bool sample_cb(repeating_timer_t *t) {
    (void)t;
    uint32_t all = gpio_get_all();
    for (int i = 0; i < NBTN; i++) {
        btn_t *b = &s_btn[i];
        bool pressed = !(all & (1u << s_pins[i]));      /* active low */
        if (pressed != b->stable) {
            if (++b->count >= JOY_DEBOUNCE_MS) {
                b->stable = pressed;
                b->count = 0;
                b->held_ms = 0;
                if (pressed) {
                    push(s_evt[i]);
                    b->next_rep = JOY_REPEAT_DELAY_MS;
                }
            }
        } else {
            b->count = 0;
            if (pressed && (s_evt[i] == JOY_UP || s_evt[i] == JOY_DOWN)) {
                b->held_ms++;
                if (b->held_ms >= b->next_rep) {
                    push(s_evt[i]);
                    b->next_rep = b->held_ms + JOY_REPEAT_RATE_MS;
                }
            }
        }
    }
    return true;
}

void joystick_init(void) {
    spsc_init(&s_q);
    for (int i = 0; i < NBTN; i++) {
        gpio_init(s_pins[i]);
        gpio_set_dir(s_pins[i], GPIO_IN);
        gpio_pull_up(s_pins[i]);
    }
    add_repeating_timer_ms(-1, sample_cb, NULL, &s_timer);
}

joy_event_t joystick_get(void) {
    if (spsc_count(&s_q) == 0) return JOY_NONE;
    joy_event_t e = (joy_event_t)s_qbuf[spsc_tail_slot(&s_q, EVQ)];
    spsc_pop_commit(&s_q);
    return e;
}
