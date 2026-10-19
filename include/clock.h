/*
 * clock.h - time of day for timestamps (core0).
 *
 * With the RV-3028 fitted, the RTC gives whole seconds and the MCU's
 * microsecond timer gives the fraction: at boot the code watches the RTC
 * seconds register until it ticks and latches the timer at that moment, so
 * real time = RTC second + (timer - latch). It re-latches every 10 minutes
 * (the RTC is the better clock over hours). Accuracy is a few ms (the poll
 * interval of the main loop), far finer than a log needs.
 *
 * Without an RTC a software clock runs from the timer; it starts "not set"
 * and is valid once the time is set in Settings > Date & time (until power
 * off).
 */
#ifndef MIDIUM_CLOCK_H
#define MIDIUM_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "timeutil.h"

#define CLOCK_RESYNC_US (600u * 1000000u)

void clock_init(void);              /* blocks up to ~1.1 s to latch the RTC second */
void clock_task(uint64_t now_us);
bool clock_rtc_present(void);
bool clock_valid(void);             /* time of day known */
bool clock_lost(void);              /* the RTC reported a power loss (PORF) at boot */
/* Real time of an uptime timestamp. false if the time is not set. */
bool clock_real(uint64_t t_us, uint32_t *secs, uint16_t *ms);
bool clock_set(const tu_datetime_t *d);

#endif
