/*
 * joystick.c - sampled every 1 ms from a repeating timer (core0 IRQ).
 * A button changes state only after it has read the same for
 * JOY_DEBOUNCE_MS consecutive samples. UP/DOWN auto-repeat while held.
 * The centre switch goes through the power-button state machine
 * (power_fsm.c): tap on release, long hold, cancel, power off.
 * Events go to core0's main loop through an SPSC queue (IRQ -> thread).
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "config.h"
#include "joystick.h"
#include "spsc.h"
#include "power_fsm.h"

#define NBTN 5
static const uint8_t s_pins[NBTN] = { PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT, PIN_JOY_PRESS };
static const joy_event_t s_evt[NBTN] = { JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_PRESS };

typedef struct {
    bool     stable;     /* debounced pressed state */
    uint8_t  count;      /* consecutive samples differing from stable */
    uint32_t held_ms;
    uint32_t next_rep;
} btn_t;

#define CENTRE 4
static btn_t s_btn[NBTN];
static repeating_timer_t s_timer;
static pbtn_t s_pb;
static uint32_t s_ms;            /* sample counter (ms) */

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
    s_ms++;
    for (int i = 0; i < NBTN; i++) {
        btn_t *b = &s_btn[i];
        bool pressed = !(all & (1u << s_pins[i]));      /* active low */
        if (pressed != b->stable) {
            if (++b->count >= JOY_DEBOUNCE_MS) {
                b->stable = pressed;
                b->count = 0;
                b->held_ms = 0;
                if (pressed && i != CENTRE) {
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
    switch (pbtn_update(&s_pb, s_btn[CENTRE].stable, s_ms)) {
    case PB_EV_TAP:    push(JOY_PRESS); break;
    case PB_EV_HOLD:   push(JOY_HOLD); break;
    case PB_EV_CANCEL: push(JOY_HOLD_CANCEL); break;
    case PB_EV_OFF:    push(JOY_HOLD_OFF); break;
    default: break;
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
    sleep_us(100);                      /* let the pull-ups settle */
    /* A centre press held now powered the box on (or woke it): start it as
     * already pressed and let the power-button machine ignore it. */
    bool held = !(gpio_get_all() & (1u << PIN_JOY_PRESS));
    s_btn[CENTRE].stable = held;
    s_ms = 0;
    pbtn_init(&s_pb, PWR_HOLD_MS, PWR_COUNTDOWN_MS, held);
    add_repeating_timer_ms(-1, sample_cb, NULL, &s_timer);
}

joy_event_t joystick_get(void) {
    if (spsc_count(&s_q) == 0) return JOY_NONE;
    joy_event_t e = (joy_event_t)s_qbuf[spsc_tail_slot(&s_q, EVQ)];
    spsc_pop_commit(&s_q);
    return e;
}
