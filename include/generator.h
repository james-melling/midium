/*
 * generator.h - test MIDI generator (core0). Complete messages go to core1 via
 * the engine's generator queue; realtime bytes via the realtime queue.
 */
#ifndef MIDIUM_GENERATOR_H
#define MIDIUM_GENERATOR_H

#include <stdint.h>
#include <stdbool.h>

void gen_init(void);
/* Call often from the core0 main loop: runs sweeps and timed note-offs. */
void gen_task(uint64_t now_us);

void gen_note_test(void);          /* C4 vel 100, off after 250 ms */
void gen_note_sweep_toggle(void);  /* C3..C5, 120 ms per note */
bool gen_note_sweep_active(void);
void gen_send_cc(void);            /* settings cc_number / cc_value */
void gen_send_pc(void);            /* settings program */
void gen_pb_sweep_toggle(void);
bool gen_pb_sweep_active(void);
void gen_clock_toggle(void);       /* F8 at settings bpm */
bool gen_clock_running(void);
void gen_clock_bpm_changed(void);
void gen_realtime(uint8_t b);      /* FA start, FB continue, FC stop */
void gen_msc(uint8_t cmd);         /* 01 GO, 02 STOP, 03 RESUME; dev 7F, fmt 7F */
void gen_mmc(uint8_t cmd);         /* 01 STOP, 02 PLAY; dev 7F */
void gen_panic(void);              /* CC123 All Notes Off on all 16 channels */

/* Text of the last thing generated (for the UI footer) and when. */
const char *gen_last_text(void);
uint64_t gen_last_time_us(void);

#endif /* MIDIUM_GENERATOR_H */
