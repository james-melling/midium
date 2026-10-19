/*
 * valedit.c - wrap-around value stepping. See valedit.h.
 */
#include "valedit.h"

int val_wrap_step(int v, int delta, int min, int max) {
    if (max <= min) return min;
    if (v < min) v = min;
    if (v > max) v = max;
    if (delta > 0) {
        if (v == max) return min;
        return (delta > max - v) ? max : v + delta;
    }
    if (delta < 0) {
        if (v == min) return max;
        return (-delta > v - min) ? min : v + delta;
    }
    return v;
}

int val_accel_step(int step, int min, int max, unsigned n) {
    if (step < 1) step = 1;
    int steps = (max - min) / step;
    if (steps > 100 * VAL_ACCEL_MIN_STEPS && n >= VAL_ACCEL_N100) return step * 100;
    if (steps > VAL_ACCEL_MIN_STEPS && n >= VAL_ACCEL_N10) return step * 10;
    return step;
}
