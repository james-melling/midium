/*
 * midi_engine.h - byte-level MIDI thru + merge engine (runs on core1).
 *
 * This file is hardware independent so it can be unit tested on a PC
 * (see test/). The RP2350 glue (UART polling, core1 loop) lives in
 * src/midi_io.c.
 *
 * Data flow
 * ---------
 *   UART0 RX (IN A) --> me_input_byte(ME_IN_A) --+--> lane A->p --+
 *   UART1 RX (IN B) --> me_input_byte(ME_IN_B) --+--> lane B->p --+--> merger p --> TX queue p --> UART
 *   core0 generator --> gen queue --> me_service() --> gen lane p --+
 *   core0 clock     --> rt queue  ------------------------------------> realtime queue p
 *   every input byte (tagged A/B) --> monitor ring --> core0
 *
 * Sources are IN A, IN B and the generator. Each output (OUT A, OUT B) has a
 * routing row that picks any combination of the three (me_settings_t).
 *
 * Rules (see README "Thru / merge engine"):
 *  - Each input has its own parser (running status, sysex, system common).
 *    A routed input byte goes into that input's lane for the output, and the
 *    output's merger moves it on at once when it may. With one source active
 *    the lane is always empty, so bytes still leave the moment they arrive
 *    (one byte time of latency, byte-identical to the input).
 *  - The merger interleaves only COMPLETE messages. While one source is part
 *    way through a message on an output (the "owner"), bytes from the other
 *    input wait in their lane, and generated messages wait in the gen lane.
 *    Sysex is a message like any other, so it is never interleaved.
 *  - Realtime bytes (F8-FF) from any input, and generated realtime, skip all
 *    of that: they go straight to the output's priority queue, anywhere in
 *    the stream (also inside sysex), as the MIDI spec allows.
 *  - Per output we remember the last status byte actually sent. A running-
 *    status message from an input re-sends its status byte if the output's
 *    last status differs (because another source sent something in between).
 *    Explicit status bytes are always passed on. Sysex and system common
 *    clear running status.
 *  - At a boundary the merger serves the generator first (bounded by
 *    ME_GEN_BACKLOG_MAX), then the inputs round-robin, one message each
 *    (a message still trickling in yields to one that is ready). A
 *    new input message starts only while the TX queue holds no more than
 *    ME_MERGE_BACKLOG_MAX bytes, so merged backlog waits in the lanes (where
 *    it can still be dropped whole) instead of in the TX queue.
 *  - Lane overflow: a message that does not fit in its lane is dropped
 *    WHOLE (the part already buffered is removed, the rest is skipped) and
 *    counted in me_out_t.merge_drops[input]. A message that has started on
 *    an output is never cut by this rule.
 *  - Stall: if the owner input stops part-way through a message for longer
 *    than ME_STALL_MSG_US (ME_STALL_SYSEX_US in sysex) while another source
 *    is waiting for that output, the boundary is forced. The rest of the
 *    stalled message is dropped on that output, because the receiver
 *    downstream has already discarded the partial message by then.
 *  - Routing changes take effect at the next message start of each input,
 *    so an output never receives half a message.
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

#define ME_NUM_INS         2
#define ME_IN_A            0
#define ME_IN_B            1

/* Sources an output can take (routing matrix columns). */
#define ME_NUM_SRCS        3
#define ME_SRC_A           0       /* = ME_IN_A */
#define ME_SRC_B           1       /* = ME_IN_B */
#define ME_SRC_GEN         2
#define ME_SRC_NONE        0xFFu

#define ME_OUTQ_SIZE       2048u   /* per-output TX byte queue (power of 2)   */
#define ME_OUT_RTQ_SIZE    32u     /* per-output priority realtime queue      */
#define ME_LANE_SIZE       1024u   /* per input, per output merge buffer (power of 2) */
#define ME_GENLANE_SIZE    256u    /* per-output generated message buffer (power of 2) */
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

/* A new input message only starts on an output whose TX queue holds no more
 * than this. With one input this never binds (the generator alone keeps the
 * queue under ME_GEN_BACKLOG_MAX + one message). When two inputs together
 * send more than one line can carry, the excess waits in the lanes and the
 * TX queue (thru latency) stays under ~82 ms + one message. */
#define ME_MERGE_BACKLOG_MAX 256u

/* Input realtime is shed (counted) instead of queued if the TX queue is
 * this full. Only reachable when merged inputs overload an output. */
#define ME_RT_SHED_FILL    1024u

/* An input message whose bytes are more than this apart is "trickling": at a
 * boundary it waits while the other input has a message ready. */
#define ME_TRICKLE_US      1000u     /* ~3 byte times */

/* Stall timeouts before another source may interrupt an input message. */
#define ME_STALL_MSG_US    100000u   /* 100 ms mid channel/common message */
#define ME_STALL_SYSEX_US  1000000u  /* 1 s mid sysex                     */

/* Routing matrix written by core0, read by core1 (single bytes, no tearing).
 * src[out][source]: source ME_SRC_A / ME_SRC_B / ME_SRC_GEN goes to out.
 * Input routes latch at each input message start. */
typedef struct {
    volatile bool src[ME_NUM_OUTS][ME_NUM_SRCS];
} me_settings_t;

/* Generated message (complete, including status / F0..F7). */
typedef struct {
    uint8_t len;
    uint8_t data[ME_GEN_MAX_LEN];
} me_gen_msg_t;

/* Input monitor entry handed to core0. */
#define ME_MON_F_GAP   0x01u   /* entries from this input were lost before this one */
#define ME_MON_F_ERR   0x02u   /* UART error marker; byte = ME_ERR_* code           */
#define ME_ERR_FRAMING 1u
#define ME_ERR_BREAK   2u
#define ME_ERR_OVERRUN 3u
typedef struct {
    uint64_t t_us;
    uint8_t  byte;
    uint8_t  flags;
    uint8_t  src;              /* ME_IN_A / ME_IN_B */
} me_mon_entry_t;

/* Lane entry: low 8 bits = byte, plus message markers. */
#define ME_L_START  0x100u     /* first byte (status) of a message */
#define ME_L_END    0x200u     /* last byte of a message */
#define ME_L_IMPL   0x400u     /* running status the input did not send: send only if needed */

typedef struct {
    uint16_t e[ME_LANE_SIZE];
    uint32_t h, t;             /* core1-private ring */
} me_lane_t;

typedef struct {
    uint8_t  q[ME_OUTQ_SIZE];
    uint32_t qh, qt;                 /* core1-private ring */
    uint8_t  rt[ME_OUT_RTQ_SIZE];
    uint32_t rh, rt_t;
    uint8_t  last_status;            /* last status byte sent (0 = none/cleared) */
    uint8_t  owner;                  /* input mid-message on this output, or ME_SRC_NONE */
    uint8_t  rr;                     /* input served first at the next boundary */
    me_lane_t lane[ME_NUM_INS];      /* input bytes waiting for this output */
    uint8_t  gl[ME_GENLANE_SIZE];    /* generated messages: [len][bytes...] */
    uint32_t gh, gt;
    volatile uint32_t drops;         /* bytes dropped because the TX queue was full (should stay 0) */
    volatile uint32_t gen_shed;      /* generated realtime dropped: output saturated */
    volatile uint32_t rt_shed;       /* input realtime dropped: output overloaded by merge */
    volatile uint32_t merge_drops[ME_NUM_INS]; /* whole input messages dropped: lane full */
} me_out_t;

typedef struct {
    volatile uint32_t bytes_in;
    volatile uint32_t orphan_data;   /* data bytes with no status (dropped) */
    volatile uint32_t uart_errors;   /* framing / break / overrun */
    volatile uint32_t mon_overflow;  /* monitor ring full (thru unaffected) */
    volatile uint32_t stalls;        /* messages cut after a stall */
} me_in_stats_t;

typedef struct {
    /* thru-side parser */
    uint8_t  running;        /* channel-voice running status, 0 = none */
    uint8_t  cur;            /* status of the message in progress, 0 = none */
    uint8_t  need, have;     /* data bytes expected / received for cur */
    bool     in_sysex;
    bool     stall_counted;  /* this message already counted as a stall */
    uint64_t last_in_us;
    bool     latched[ME_NUM_OUTS];   /* current message goes to this output */
    bool     skip[ME_NUM_OUTS];      /* dropping the rest of the current message */
    uint32_t msg_start[ME_NUM_OUTS]; /* lane index of the current message's start */
    me_in_stats_t st;
} me_in_t;

typedef struct {
    volatile uint32_t gen_msgs;      /* generated messages accepted */
    volatile uint32_t gen_rt;        /* generated realtime bytes processed */
    volatile uint32_t stalls;        /* forced boundaries after input stall (all inputs) */
} me_stats_t;

typedef struct {
    me_in_t  in[ME_NUM_INS];
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
    uint8_t        mon_gap;          /* bit per input: entries lost */

    me_stats_t stats;
} me_t;

/* ---- setup (before core1 starts) ---- */
void me_init(me_t *me, me_settings_t *set);

/* ---- core1 side ---- */
void me_input_byte(me_t *me, int in, uint8_t b, uint64_t now_us);
void me_service(me_t *me, uint64_t now_us);
bool me_out_pop(me_t *me, int port, uint8_t *b);
uint32_t me_out_pending(const me_t *me, int port);   /* TX + realtime queue */
bool me_out_at_boundary(const me_t *me, int port);   /* no message in progress */
bool me_idle(const me_t *me);   /* nothing waiting anywhere (lanes, queues) */
void me_mon_push(me_t *me, int in, uint8_t b, uint8_t flags, uint64_t t_us);

/* ---- core0 side ---- */
bool me_gen_push(me_t *me, const uint8_t *msg, uint8_t len); /* thread only */
bool me_gen_rt_push(me_t *me, uint8_t rt);  /* caller must serialise producers */
bool me_mon_pop(me_t *me, me_mon_entry_t *e);

#endif /* MIDIUM_MIDI_ENGINE_H */
