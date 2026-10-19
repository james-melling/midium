/*
 * rv3028.c - RV-3028-C7 RTC driver. See rv3028.h.
 *
 * Backup switchover: the chip ships with switchover disabled (BSM = 00), so
 * a backup cell would never be used. Direct switching mode (DSM) changes to
 * VBACKUP as soon as VDD < VBACKUP, which suits a 3 V lithium cell or a
 * supercap below the 3.3 V rail. It is set in the RAM mirror and copied to
 * EEPROM (only when it differs, so the EEPROM is written once per chip).
 */
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "pins.h"
#include "rv3028.h"

#define I2C_TIMEOUT_US 5000u

static bool s_present;
static bool s_lost;
static uint8_t s_bsm;

static bool rd(uint8_t reg, uint8_t *dst, size_t n) {
    if (i2c_write_timeout_us(DISPLAY_I2C, RV3028_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(DISPLAY_I2C, RV3028_I2C_ADDR, dst, n, false, I2C_TIMEOUT_US) == (int)n;
}

static bool wr(uint8_t reg, const uint8_t *src, size_t n) {
    uint8_t b[9];
    if (n > 8) return false;
    b[0] = reg;
    for (size_t i = 0; i < n; i++) b[1 + i] = src[i];
    return i2c_write_timeout_us(DISPLAY_I2C, RV3028_I2C_ADDR, b, n + 1, false, I2C_TIMEOUT_US) == (int)(n + 1);
}

static bool wr1(uint8_t reg, uint8_t v) { return wr(reg, &v, 1); }

static void ensure_dsm(void) {
    uint8_t bk;
    if (!rd(RV3028_EE_BACKUP, &bk, 1)) return;
    s_bsm = bk & RV3028_BSM_MASK;
    if (s_bsm == RV3028_BSM_DSM) return;
    uint8_t c1;
    if (!rd(RV3028_CTRL1, &c1, 1)) return;
    wr1(RV3028_CTRL1, (uint8_t)(c1 | RV3028_C1_EERD));             /* hold auto refresh */
    wr1(RV3028_EE_BACKUP, (uint8_t)((bk & ~RV3028_BSM_MASK) | RV3028_BSM_DSM));
    wr1(RV3028_EECMD, 0x00);
    wr1(RV3028_EECMD, 0x11);                                        /* update EEPROM */
    for (int i = 0; i < 100; i++) {                                 /* ~63 ms typical */
        uint8_t st;
        sleep_ms(2);
        if (rd(RV3028_STATUS, &st, 1) && !(st & RV3028_ST_EEBUSY)) break;
    }
    wr1(RV3028_CTRL1, (uint8_t)(c1 & ~RV3028_C1_EERD));
    if (rd(RV3028_EE_BACKUP, &bk, 1)) s_bsm = bk & RV3028_BSM_MASK;
}

bool rv3028_init(void) {
    uint8_t st;
    s_present = rd(RV3028_STATUS, &st, 1);
    if (!s_present) return false;
    s_lost = (st & RV3028_ST_PORF) != 0;
    uint8_t c2;
    if (rd(RV3028_CTRL2, &c2, 1) && (c2 & RV3028_C2_12_24)) wr1(RV3028_CTRL2, (uint8_t)(c2 & ~RV3028_C2_12_24));
    ensure_dsm();
    return true;
}

bool rv3028_present(void) { return s_present; }
bool rv3028_time_lost(void) { return s_lost; }
uint8_t rv3028_backup_mode(void) { return s_bsm; }

bool rv3028_read_seconds(uint8_t *sec) {
    uint8_t b;
    if (!s_present || !rd(RV3028_SECONDS, &b, 1)) return false;
    *sec = tu_from_bcd(b & 0x7Fu);
    return true;
}

bool rv3028_read(tu_datetime_t *d) {
    uint8_t r[7];
    if (!s_present || !rd(RV3028_SECONDS, r, 7)) return false;
    d->sec = tu_from_bcd(r[0] & 0x7Fu);
    d->min = tu_from_bcd(r[1] & 0x7Fu);
    d->hour = tu_from_bcd(r[2] & 0x3Fu);
    d->day = tu_from_bcd(r[4] & 0x3Fu);
    d->month = tu_from_bcd(r[5] & 0x1Fu);
    d->year = (uint16_t)(2000u + tu_from_bcd(r[6]));
    return tu_valid(d);
}

bool rv3028_write(const tu_datetime_t *d) {
    if (!s_present || !tu_valid(d)) return false;
    uint8_t r[7] = {
        tu_to_bcd(d->sec), tu_to_bcd(d->min), tu_to_bcd(d->hour),
        tu_weekday(tu_to_secs(d)), tu_to_bcd(d->day), tu_to_bcd(d->month),
        tu_to_bcd((uint8_t)(d->year - 2000u)),
    };
    if (!wr(RV3028_SECONDS, r, 7)) return false;
    uint8_t st;
    if (rd(RV3028_STATUS, &st, 1) && (st & RV3028_ST_PORF)) wr1(RV3028_STATUS, (uint8_t)(st & ~RV3028_ST_PORF));
    s_lost = false;
    return true;
}
