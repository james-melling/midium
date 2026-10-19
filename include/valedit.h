/*
 * valedit.h - shared value-editing rules for every menu value and picker
 * (portable, unit tested).
 *
 * Values WRAP at both ends: UP on the maximum gives the minimum, DOWN on the
 * minimum gives the maximum. A larger (accelerated) step that would pass an
 * end stops on that end first, so no value is skipped; the next step wraps.
 */
#ifndef MIDIUM_VALEDIT_H
#define MIDIUM_VALEDIT_H

#include <stdint.h>

/* v moved by delta (> 0 up, < 0 down) within [min, max], wrapping. */
int val_wrap_step(int v, int delta, int min, int max);

/* Hold acceleration: the step for the n-th event of one held direction
 * (n = 0 for the first press, then 1, 2, ... for each auto-repeat).
 * Ranges of more than VAL_ACCEL_MIN_STEPS steps go 10x after VAL_ACCEL_N10
 * repeats (about 1.3 s of holding) and 100x after VAL_ACCEL_N100 repeats when
 * the range has more than 100x VAL_ACCEL_MIN_STEPS steps. */
#define VAL_ACCEL_MIN_STEPS 150
#define VAL_ACCEL_N10       12u
#define VAL_ACCEL_N100      36u
int val_accel_step(int step, int min, int max, unsigned n);

#endif
