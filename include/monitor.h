/*
 * monitor.h - core0 side of the input monitor: drains the engine's input
 * ring, parses it (one parser per input), keeps a history of recent messages
 * tagged with their input, and per-type and per-input counters.
 */
#ifndef MIDIUM_MONITOR_H
#define MIDIUM_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "midi_parser.h"
#include "mtc.h"

void monitor_init(void);

/* Process up to max_bytes from the input ring. Returns true if the visible
 * history changed. */
bool monitor_poll(uint32_t max_bytes);

/* History is addressed by absolute sequence number. */
uint32_t monitor_next_seq(void);      /* one past newest */
uint32_t monitor_oldest_seq(void);
bool     monitor_get(uint32_t seq, midi_msg_t *out);

uint32_t monitor_type_count(midi_type_t t);
uint32_t monitor_total_msgs(void);
uint32_t monitor_input_msgs(int in);  /* messages from ME_IN_A / ME_IN_B */
uint32_t monitor_stray_bytes(void);
void     monitor_clear(void);

/* MTC readers, one per input (fed with every message, unfiltered). */
const mtc_t *monitor_mtc(int in);
/* Input with the most recent MTC (quarter or full frame), -1 if none ever. */
int      monitor_mtc_active(void);

#endif /* MIDIUM_MONITOR_H */
