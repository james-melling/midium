/*
 * midi_engine.h - byte-level MIDI thru + merge engine (runs on core1).
 *
 * This file is hardware independent so it can be unit tested on a PC
 * (see test/). The RP2350 glue (UART polling, core1 loop) lives in
 * src/midi_core1.c.
 *
 * Data flow
 * ---------
 *   UART0 RX byte --> me_input_byte() --+--> per-output TX queues --> UARTs
 *                                       |
 *                                       +--> (glue) monitor ring --> core0
 *
 *   core0 generator --> gen queue (complete messages) --+
 *   core0 clock timer --> rt queue (single realtime bytes) --> me_service()
 *
 * Rules (see README "Thru / merge engine"):
 *  - Input bytes are forwarded to each enabled output the moment they arrive.
 *  - Realtime bytes (F8-FF), input or generated, may go anywhere in the
 *    stream and jump ahead of anything queued for that output.
 *  - Per output we remember the last status byte actually sent. When an input
 *    data byte starts a new running-status message and that output's last
 *    status differs (because a generated message was merged in), the running
 *    status byte is re-sent first.
 *  - Generated non-realtime messages are only inserted at an input message
 *    boundary (not mid-message, not mid-sysex). If the input stalls part-way
 *    through a message for longer than a timeout, the boundary is forced and
 *    the remainder of that stalled input message is dropped on the outputs
 *    that received the generated message (the receiver downstream has already
 *    discarded the partial message by then).
 *  - Thru on/off changes take effect at the next input message start so an
 *    output never receives half a message.
 */
#ifndef MIDIUM_MIDI_ENGINE_H
#define MIDIUM_MIDI_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include "spsc.h"

/* On target, run the hot path from SRAM (same section the Pico SDK's
 * __not_in_flash_func uses) so XIP flash stalls cannot delay thru bytes. */
#ifndef ME_RAMFUNC
#if defined(MIDIUM_TARGET) && MIDIUM_TARGET
#define ME_RAMFUNC(f) __attribute__((section(".time_critical.me_" #f))) f
#else
#define ME_RAMFUNC(f) f
#endif
#endif

#define ME_NUM_OUTS        2
#define ME_OUT_A           0
#define ME_OUT_B           1

#define ME_OUTQ_SIZE       1024u   /* per-output TX byte queue (power of 2)   */
#define ME_OUT_RTQ_SIZE    32u     /* per-output priority realtime queue      */
#define ME_GEN_MAX_LEN     64u     /* max generated message (sysex) length    */
#define ME_GENQ_SIZE       32u     /* generated message queue entries         */
#define ME_RTQ_SIZE        64u     /* generated realtime byte queue           */
#define ME_MONQ_SIZE       4096u   /* input monitor ring for core0            */

/* A generated message is only merged into an output whose TX queue holds no
 * more than this many bytes afterwards. On an input line that is already
 * 100% busy this caps the extra thru latency the generator can cause at
 * ~128 x 320 us = 41 ms; with any spare line capacity the queue drains and
 * thru latency returns to one byte time. */
#define ME_GEN_BACKLOG_MAX 128u

/* Stall timeouts before a generated message may interrupt the input. */
#define ME_STALL_MSG_US    100000u   /* 100 ms mid channel/common message */
#define ME_STALL_SYSEX_US  1000000u  /* 1 s mid sysex                     */

/* Settings written by core0, read by core1 (single bytes, no tearing). */
typedef struct {
    volatile bool thru[ME_NUM_OUTS];   /* forward input to this output        */
    volatile bool gen[ME_NUM_OUTS];    /* generator messages go to this output */
} me_settings_t;

/* Generated message (complete, including status / F0..F7). */
typedef struct {
    uint8_t len;
    uint8_t data[ME_GEN_MAX_LEN];
} me_gen_msg_t;

/* Input monitor entry handed to core0. */
#define ME_MON_F_GAP   0x01u   /* entries were lost before this one (ring full) */
#define ME_MON_F_ERR   0x02u   /* UART error marker; byte = ME_ERR_* code       */
#define ME_ERR_FRAMING 1u
#define ME_ERR_BREAK   2u
#define ME_ERR_OVERRUN 3u
typedef struct {
    uint64_t t_us;
    uint8_t  byte;
    uint8_t  flags;
} me_mon_entry_t;

typedef struct {
    uint8_t  q[ME_OUTQ_SIZE];
    uint32_t qh, qt;                 /* core1-private ring */
    uint8_t  rt[ME_OUT_RTQ_SIZE];
    uint32_t rh, rt_t;
    uint8_t  last_status;            /* last status byte sent (0 = none/cleared) */
    bool     latched;                /* thru active for the current input message */
    bool     skip;                   /* dropping rest of the current input message */
    volatile uint32_t drops;         /* bytes dropped because the TX queue was full */
    volatile uint32_t gen_shed;      /* generated realtime dropped: output saturated */
} me_out_t;

typedef struct {
    volatile uint32_t bytes_in;
    volatile uint32_t orphan_data;   /* data bytes with no status (dropped) */
    volatile uint32_t gen_msgs;      /* generated messages sent */
    volatile uint32_t gen_rt;        /* generated realtime bytes processed */
    volatile uint32_t stalls;        /* forced boundaries after input stall */
    volatile uint32_t mon_overflow;  /* monitor ring full (thru unaffected) */
    volatile uint32_t uart_errors;   /* framing / break / overrun */
} me_stats_t;

typedef struct {
    /* input parser state (thru side) */
    uint8_t  running;        /* channel-voice running status, 0 = none */
    uint8_t  cur;            /* status of the message in progress, 0 = none */
    uint8_t  need, have;     /* data bytes expected / received for cur */
    bool     in_sysex;
    bool     forced;         /* boundary forced by stall timeout */
    uint64_t last_in_us;

    me_out_t out[ME_NUM_OUTS];
    me_settings_t *set;

    /* core0 -> core1 */
    spsc_t        genq_idx;
    me_gen_msg_t  genq[ME_GENQ_SIZE];
    spsc_t        rtq_idx;
    uint8_t       rtq[ME_RTQ_SIZE];

    /* core1 -> core0 */
    spsc_t         mon_idx;
    me_mon_entry_t mon[ME_MONQ_SIZE];
    bool           mon_gap;

    me_stats_t stats;
} me_t;

/* ---- setup (before core1 starts) ---- */
void me_init(me_t *me, me_settings_t *set);

/* ---- core1 side ---- */
void me_input_byte(me_t *me, uint8_t b, uint64_t now_us);
void me_service(me_t *me, uint64_t now_us);
bool me_out_pop(me_t *me, int port, uint8_t *b);
uint32_t me_out_pending(const me_t *me, int port);
bool me_at_boundary(const me_t *me);
void me_mon_push(me_t *me, uint8_t b, uint8_t flags, uint64_t t_us);

/* ---- core0 side ---- */
bool me_gen_push(me_t *me, const uint8_t *msg, uint8_t len); /* thread only */
bool me_gen_rt_push(me_t *me, uint8_t rt);  /* caller must serialise producers */
bool me_mon_pop(me_t *me, me_mon_entry_t *e);

#endif /* MIDIUM_MIDI_ENGINE_H */
