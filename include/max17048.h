/*
 * max17048.h - Maxim/ADI MAX17048 single-cell Li-ion fuel gauge (ModelGauge)
 * on I2C1 with the OLED and the RTC, 7-bit address 0x36.
 *
 * Registers are 16 bits, MSB first. From the datasheet as remembered when
 * this was written - VERIFY against the current MAX17048/MAX17049 datasheet
 * before the PCB:
 *   02 VCELL    cell voltage, 78.125 uV per LSB (whole 16 bits)
 *   04 SOC      state of charge, high byte = %, low byte = 1/256 %
 *               (can read slightly over 100 % at the end of charge)
 *   06 MODE     write 0x4000 = quick-start (not used: it re-guesses SOC)
 *   08 VERSION  production version, 0x001x
 *   0A HIBRT    hibernate thresholds (left at the default)
 *   0C CONFIG   high byte RCOMP (default 0x97); low byte: bit 7 SLEEP,
 *               bit 6 ALSC, bit 5 ALRT (alert flag, write 0 to clear),
 *               bits 4:0 ATHD: empty alert at (32 - ATHD) %
 *   16 CRATE    charge / discharge rate, signed, 0.208 %/hour per LSB
 *   1A STATUS   alert flags (not used)
 * The ALRT pin is not wired (the firmware polls once a second).
 */
#ifndef MIDIUM_MAX17048_H
#define MIDIUM_MAX17048_H

#include <stdint.h>
#include <stdbool.h>
#include "pins.h"

#define MAX17048_I2C_ADDR   GAUGE_I2C_ADDR
#define MAX17048_VCELL      0x02
#define MAX17048_SOC        0x04
#define MAX17048_MODE       0x06
#define MAX17048_VERSION    0x08
#define MAX17048_HIBRT      0x0A
#define MAX17048_CONFIG     0x0C
#define MAX17048_CRATE      0x16
#define MAX17048_STATUS     0x1A
#define MAX17048_CFG_SLEEP  0x80u
#define MAX17048_CFG_ALSC   0x40u
#define MAX17048_CFG_ALRT   0x20u
#define MAX17048_CFG_ATHD   0x1Fu

/* ---- conversions (portable, unit tested) ---- */
/* VCELL raw -> millivolts (78.125 uV = 5/64 mV per LSB), rounded. */
static inline unsigned max17048_vcell_mv(uint16_t raw) { return ((unsigned)raw * 5u + 32u) / 64u; }
/* SOC raw -> whole percent, clamped to 100. */
static inline unsigned max17048_soc_pct(uint16_t raw) { unsigned p = raw >> 8; return p > 100u ? 100u : p; }
/* SOC raw -> tenths of a percent (rounded), clamped to 1000. */
static inline unsigned max17048_soc_x10(uint16_t raw) {
    unsigned p = ((unsigned)raw * 10u + 128u) / 256u;
    return p > 1000u ? 1000u : p;
}
/* CRATE raw -> thousandths of a percent per hour (0.208 %/h = 208). */
static inline int32_t max17048_crate_x1000(uint16_t raw) { return (int32_t)(int16_t)raw * 208; }
/* Alert threshold field for an empty alert at pct (1..32 %). */
static inline uint8_t max17048_athd(unsigned pct) {
    if (pct < 1u) pct = 1u;
    if (pct > 32u) pct = 32u;
    return (uint8_t)(32u - pct);
}
/* New CONFIG value: RCOMP and SLEEP / ALSC kept, ALRT cleared, ATHD set. */
static inline uint16_t max17048_config_with_athd(uint16_t cfg, unsigned pct) {
    uint8_t lo = (uint8_t)((cfg & (MAX17048_CFG_SLEEP | MAX17048_CFG_ALSC)) | max17048_athd(pct));
    return (uint16_t)((cfg & 0xFF00u) | lo);
}
/* VERSION reads 0x001x on the MAX17048 / MAX17049. */
static inline bool max17048_version_ok(uint16_t v) { return (v & 0xFFF0u) == 0x0010u; }

/* ---- driver (core0, I2C1) ---- */
typedef struct {
    uint16_t vcell, soc, crate;      /* raw registers */
} max17048_sample_t;

/* Probe 0x36 (VERSION), set the empty alert to alert_pct and clear ALRT. */
bool     max17048_init(unsigned alert_pct);
bool     max17048_present(void);
uint16_t max17048_version(void);
bool     max17048_read(max17048_sample_t *s);

#endif /* MIDIUM_MAX17048_H */
