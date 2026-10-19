/*
 * max17048.c - MAX17048 fuel gauge driver. See max17048.h.
 */
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "pins.h"
#include "max17048.h"

#define I2C_TIMEOUT_US 5000u

static bool     s_present;
static uint16_t s_version;

static bool rd16(uint8_t reg, uint16_t *v) {
    uint8_t b[2];
    if (i2c_write_timeout_us(DISPLAY_I2C, MAX17048_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(DISPLAY_I2C, MAX17048_I2C_ADDR, b, 2, false, I2C_TIMEOUT_US) != 2) return false;
    *v = (uint16_t)(b[0] << 8 | b[1]);
    return true;
}

static bool wr16(uint8_t reg, uint16_t v) {
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_write_timeout_us(DISPLAY_I2C, MAX17048_I2C_ADDR, b, 3, false, I2C_TIMEOUT_US) == 3;
}

bool max17048_init(unsigned alert_pct) {
    uint16_t v, cfg;
    s_present = rd16(MAX17048_VERSION, &v) && max17048_version_ok(v);
    if (!s_present) return false;
    s_version = v;
    if (rd16(MAX17048_CONFIG, &cfg)) {
        uint16_t want = max17048_config_with_athd(cfg, alert_pct);
        if (want != cfg) wr16(MAX17048_CONFIG, want);
    }
    return true;
}

bool max17048_present(void) { return s_present; }
uint16_t max17048_version(void) { return s_version; }

bool max17048_read(max17048_sample_t *s) {
    if (!s_present) return false;
    return rd16(MAX17048_VCELL, &s->vcell) && rd16(MAX17048_SOC, &s->soc) && rd16(MAX17048_CRATE, &s->crate);
}
