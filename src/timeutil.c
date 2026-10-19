/*
 * timeutil.c - calendar helpers. See timeutil.h.
 */
#include <stdio.h>
#include <string.h>
#include "timeutil.h"

bool tu_is_leap(uint16_t y) { return (y % 4u == 0 && y % 100u != 0) || y % 400u == 0; }

uint8_t tu_days_in_month(uint16_t y, uint8_t m) {
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m < 1 || m > 12) return 0;
    return (uint8_t)(D[m - 1] + (m == 2 && tu_is_leap(y) ? 1 : 0));
}

bool tu_valid(const tu_datetime_t *d) {
    return d->year >= TU_YEAR_MIN && d->year <= TU_YEAR_MAX && d->month >= 1 && d->month <= 12 &&
           d->day >= 1 && d->day <= tu_days_in_month(d->year, d->month) &&
           d->hour < 24 && d->min < 60 && d->sec < 60;
}

uint32_t tu_to_secs(const tu_datetime_t *d) {
    uint32_t days = 0;
    for (uint16_t y = TU_YEAR_MIN; y < d->year; y++) days += tu_is_leap(y) ? 366u : 365u;
    for (uint8_t m = 1; m < d->month; m++) days += tu_days_in_month(d->year, m);
    days += (uint32_t)d->day - 1u;
    return ((days * 24u + d->hour) * 60u + d->min) * 60u + d->sec;
}

void tu_from_secs(uint32_t secs, tu_datetime_t *d) {
    uint32_t days = secs / 86400u, rem = secs % 86400u;
    d->hour = (uint8_t)(rem / 3600u);
    d->min = (uint8_t)((rem / 60u) % 60u);
    d->sec = (uint8_t)(rem % 60u);
    uint16_t y = TU_YEAR_MIN;
    for (;;) {
        uint32_t yd = tu_is_leap(y) ? 366u : 365u;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    uint8_t m = 1;
    for (;;) {
        uint32_t md = tu_days_in_month(y, m);
        if (days < md) break;
        days -= md;
        m++;
    }
    d->year = y;
    d->month = m;
    d->day = (uint8_t)(days + 1u);
}

uint8_t tu_weekday(uint32_t secs) { return (uint8_t)((secs / 86400u + 6u) % 7u); }

void tu_fmt_tod(uint32_t secs, uint16_t ms, bool with_ms, char *buf, size_t n) {
    uint32_t r = secs % 86400u;
    unsigned h = (unsigned)(r / 3600u), m = (unsigned)((r / 60u) % 60u), s = (unsigned)(r % 60u);
    if (with_ms) snprintf(buf, n, "%02u:%02u:%02u.%03u", h, m, s, (unsigned)(ms % 1000u));
    else snprintf(buf, n, "%02u:%02u:%02u", h, m, s);
}

void tu_fmt_date(uint32_t secs, char *buf, size_t n) {
    tu_datetime_t d;
    tu_from_secs(secs, &d);
    snprintf(buf, n, "%04u-%02u-%02u", (unsigned)d.year, (unsigned)d.month, (unsigned)d.day);
}

void tu_fmt_duration(uint32_t secs, char *buf, size_t n) {
    snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(secs / 3600u),
             (unsigned long)((secs / 60u) % 60u), (unsigned long)(secs % 60u));
}

bool tu_parse_build(const char *date, const char *time, tu_datetime_t *d) {
    static const char M[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    if (!date || !time || strlen(date) < 11 || strlen(time) < 8) return false;
    int mon = 0;
    for (int i = 0; i < 12; i++)
        if (strncmp(date, &M[i * 3], 3) == 0) mon = i + 1;
    unsigned day = 0, year = 0, h = 0, mi = 0, s = 0;
    if (!mon || sscanf(date + 4, "%u %u", &day, &year) != 2) return false;
    if (sscanf(time, "%u:%u:%u", &h, &mi, &s) != 3) return false;
    d->year = (uint16_t)year; d->month = (uint8_t)mon; d->day = (uint8_t)day;
    d->hour = (uint8_t)h; d->min = (uint8_t)mi; d->sec = (uint8_t)s;
    return tu_valid(d);
}
