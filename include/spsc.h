/*
 * spsc.h - lock-free single-producer / single-consumer ring index.
 *
 * The ring stores only the head/tail counters; the caller owns the storage
 * array (power-of-two length). Counters are free-running uint32_t, so
 * head - tail is the fill level even across wrap.
 *
 * Producer: spsc_free() / write slot spsc_head_slot() / spsc_push_commit().
 * Consumer: spsc_count() / read slot spsc_tail_slot() / spsc_pop_commit().
 *
 * Acquire/release atomics give the barriers needed between the RP2350's two
 * Cortex-M33 cores (DMB) and also between an IRQ and thread code. Works
 * unchanged on the host for unit tests.
 */
#ifndef MIDIUM_SPSC_H
#define MIDIUM_SPSC_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t head; /* written by producer only */
    uint32_t tail; /* written by consumer only */
} spsc_t;

static inline void spsc_init(spsc_t *q) {
    __atomic_store_n(&q->head, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&q->tail, 0u, __ATOMIC_RELAXED);
}

/* ---- producer side ---- */
static inline uint32_t spsc_free(const spsc_t *q, uint32_t size) {
    uint32_t h = __atomic_load_n(&q->head, __ATOMIC_RELAXED);
    uint32_t t = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
    return size - (h - t);
}
static inline uint32_t spsc_head_slot(const spsc_t *q, uint32_t size) {
    return __atomic_load_n(&q->head, __ATOMIC_RELAXED) & (size - 1u);
}
static inline void spsc_push_commit(spsc_t *q) {
    uint32_t h = __atomic_load_n(&q->head, __ATOMIC_RELAXED);
    __atomic_store_n(&q->head, h + 1u, __ATOMIC_RELEASE);
}

/* ---- consumer side ---- */
static inline uint32_t spsc_count(const spsc_t *q) {
    uint32_t h = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    uint32_t t = __atomic_load_n(&q->tail, __ATOMIC_RELAXED);
    return h - t;
}
static inline uint32_t spsc_tail_slot(const spsc_t *q, uint32_t size) {
    return __atomic_load_n(&q->tail, __ATOMIC_RELAXED) & (size - 1u);
}
static inline void spsc_pop_commit(spsc_t *q) {
    uint32_t t = __atomic_load_n(&q->tail, __ATOMIC_RELAXED);
    __atomic_store_n(&q->tail, t + 1u, __ATOMIC_RELEASE);
}

#endif /* MIDIUM_SPSC_H */
