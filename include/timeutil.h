/*
 * timeutil.h - calendar time helpers (portable C, unit tested).
 *
 * Real time is kept as seconds since 2000-01-01 00:00:00 (local time, no
 * time zone: the RTC holds wall-clock time) plus milliseconds. The range is
 * 2000-01-01 to 2099-12-31, the range of the RV-3028's two-digit year.
 */
#ifndef MIDIUM_TIMEUTIL_H
#define MIDIUM_TIMEUTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TU_YEAR_MIN 2000u
#define TU_YEAR_MAX 2099u

typedef struct {
    uint16_t year;    /* 2000..2099 */
    uint8_t  month;   /* 1..12 */
    uint8_t  day;     /* 1..31 */
    uint8_t  hour;    /* 0..23 */
    uint8_t  min;     /* 0..59 */
    uint8_t  sec;     /* 0..59 */
} tu_datetime_t;

bool     tu_is_leap(uint16_t year);
uint8_t  tu_days_in_month(uint16_t year, uint8_t month);
bool     tu_valid(const tu_datetime_t *d);
/* Seconds since 2000-01-01 00:00:00. d must be valid. */
uint32_t tu_to_secs(const tu_datetime_t *d);
void     tu_from_secs(uint32_t secs, tu_datetime_t *d);
/* 0 = Sunday .. 6 = Saturday (2000-01-01 was a Saturday). */
uint8_t  tu_weekday(uint32_t secs);

static inline uint8_t tu_to_bcd(uint8_t v)   { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }
static inline uint8_t tu_from_bcd(uint8_t b) { return (uint8_t)((b >> 4) * 10u + (b & 0x0Fu)); }

/* "hh:mm:ss.mmm" (with_ms) or "hh:mm:ss". buf >= 13. */
void tu_fmt_tod(uint32_t secs, uint16_t ms, bool with_ms, char *buf, size_t n);
/* "YYYY-MM-DD". buf >= 11. */
void tu_fmt_date(uint32_t secs, char *buf, size_t n);
/* Duration "h:mm:ss" (hours keep counting). buf >= 12. */
void tu_fmt_duration(uint32_t secs, char *buf, size_t n);

/* Build date (__DATE__ "Oct 19 2026", __TIME__ "14:03:22") as a datetime;
 * used as the starting point of the clock editor when no time is set. */
bool tu_parse_build(const char *date, const char *time, tu_datetime_t *d);

#endif /* MIDIUM_TIMEUTIL_H */
