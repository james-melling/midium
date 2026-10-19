/*
 * clock.c - time of day from the RV-3028 (or a software clock). See clock.h.
 */
#include "pico/stdlib.h"

#include "clock.h"
#include "rv3028.h"
#include "display.h"

static bool     s_rtc;
static bool     s_valid;
static bool     s_lost;
static uint64_t s_base_us;          /* timer at a whole second */
static uint32_t s_base_secs;        /* that second, since 2000-01-01 */

/* resync state */
static uint64_t s_next_sync;
static bool     s_syncing;
static uint8_t  s_sync_sec;
static uint64_t s_sync_start, s_sync_last;

static bool latch_now(uint64_t now) {
    tu_datetime_t d;
    if (!rv3028_read(&d)) return false;
    s_base_secs = tu_to_secs(&d);
    s_base_us = now;
    return true;
}

void clock_init(void) {
    i2c1_bus_init();
    s_rtc = rv3028_init();
    if (!s_rtc) return;
    s_lost = rv3028_time_lost();
    if (s_lost) return;                         /* time invalid until it is set */
    uint8_t first, sec;
    if (!rv3028_read_seconds(&first)) return;
    uint64_t t0 = time_us_64();
    while (time_us_64() - t0 < 1100000u) {
        sleep_us(500);
        if (!rv3028_read_seconds(&sec)) return;
        if (sec != first) {
            s_valid = latch_now(time_us_64());
            break;
        }
    }
    if (!s_valid) s_valid = latch_now(time_us_64());   /* no tick seen: within 1 s */
    s_next_sync = time_us_64() + CLOCK_RESYNC_US;
}

void clock_task(uint64_t now) {
    if (!s_rtc || !s_valid) return;
    if (!s_syncing) {
        /* start polling just before the expected second boundary */
        if (now < s_next_sync) return;
        uint64_t phase = (now - s_base_us) % 1000000u;
        if (phase < 990000u) return;
        if (!rv3028_read_seconds(&s_sync_sec)) { s_next_sync = now + 60000000u; return; }
        s_syncing = true;
        s_sync_start = s_sync_last = now;
        return;
    }
    if (now - s_sync_last < 300u) return;
    s_sync_last = now;
    uint8_t sec;
    if (!rv3028_read_seconds(&sec) || now - s_sync_start > 1500000u) {
        s_syncing = false;
        s_next_sync = now + 60000000u;
        return;
    }
    if (sec != s_sync_sec) {
        latch_now(now);
        s_syncing = false;
        s_next_sync = now + CLOCK_RESYNC_US;
    }
}

bool clock_rtc_present(void) { return s_rtc; }
bool clock_valid(void) { return s_valid; }
bool clock_lost(void) { return s_lost; }

bool clock_real(uint64_t t_us, uint32_t *secs, uint16_t *ms) {
    if (!s_valid) return false;
    int64_t d = (int64_t)t_us - (int64_t)s_base_us;
    int64_t dms = d >= 0 ? d / 1000 : -((-d + 999) / 1000);
    int64_t total = (int64_t)s_base_secs * 1000 + dms;
    if (total < 0) total = 0;
    *secs = (uint32_t)(total / 1000);
    *ms = (uint16_t)(total % 1000);
    return true;
}

bool clock_set(const tu_datetime_t *d) {
    if (!tu_valid(d)) return false;
    uint64_t now = time_us_64();
    if (s_rtc && !rv3028_write(d)) return false;
    s_base_secs = tu_to_secs(d);
    s_base_us = now;
    s_valid = true;
    s_lost = false;
    s_syncing = false;
    s_next_sync = now + CLOCK_RESYNC_US;
    return true;
}
