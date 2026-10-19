/*
 * logger.h - logging sessions (core0): every message the monitor parses goes
 * to the flash log (unfiltered) with its input and timestamp (real time when
 * the clock is set, else uptime). See log_store.h for the format.
 *
 * Flash work is paced from the main loop: a full page is programmed as soon
 * as it is ready (~1 ms with core0 interrupts off, core1 keeps running), a
 * part-filled page is flushed after LOGGER_FLUSH_US, and sector erases
 * (~45 ms typical) wait for a quiet moment (no input for LOGGER_QUIET_US and
 * the clock generator off) unless the RAM queue is half full.
 */
#ifndef MIDIUM_LOGGER_H
#define MIDIUM_LOGGER_H

#include <stdint.h>
#include <stdbool.h>
#include "log_store.h"

#define LOGGER_FLUSH_US 2000000u
#define LOGGER_QUIET_US 100000u

/* fl = NULL: logging disabled. Starts the boot session. */
void   logger_init(const log_flash_t *fl);
void   logger_msg(const midi_msg_t *m);
void   logger_task(uint64_t now_us);
void   logger_new_session(void);
/* Write everything held in RAM to flash now (before browsing the live session). */
void   logger_flush_now(void);
void   logger_delete_all(void);
log_t *logger_log(void);
bool   logger_enabled(void);
bool   logger_time(uint64_t t_us, log_time_t *t);

#endif
