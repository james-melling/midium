/*
 * sim_hal.h - the slice of the Pico SDK that MIDIum's core0 modules use,
 * reimplemented for the browser simulator (single-threaded, simulated clock).
 *
 * The firmware sources are compiled unchanged against these declarations:
 *   ui.c, generator.c, joystick.c, debug_log.c, disp_tft.c, disp_oled.c,
 *   rv3028.c, clock.c, logger.c (the flash log uses log_flash_ram.c).
 * SPI and I2C writes are fed to small ST7789 / SSD1306 controller emulators
 * that paint an RGBA framebuffer, so the real display drivers run as-is, and
 * I2C to 0x52 reaches an RV-3028 register emulation running on sim time,
 * and 0x36 a MAX17048 fuel gauge whose charge the page sets (charging on
 * simulated USB raises it). GP24 reads the simulated USB (VBUS), and the
 * level the firmware drives on PIN_PWR_KILL is reported to the page, which
 * plays the push-button power controller.
 * midi_io.c (UARTs, core1, relays, watchdog) is replaced by sim_io.c.
 */
#ifndef MIDIUM_SIM_HAL_H
#define MIDIUM_SIM_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef unsigned int uint;

/* ---- time ---------------------------------------------------------------- */
uint64_t time_us_64(void);
/* Advance simulated time; core1 and timer IRQs keep running meanwhile. */
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

/* ---- repeating timers (fired from the sim scheduler, "IRQ context") ------ */
typedef struct repeating_timer repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
struct repeating_timer {
    int64_t delay_us;
    void *user_data;
    repeating_timer_callback_t callback;
    uint64_t next_us;     /* sim: next fire time */
    bool active;          /* sim */
};
bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t cb, void *user, repeating_timer_t *out);
bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t cb, void *user, repeating_timer_t *out);
bool cancel_repeating_timer(repeating_timer_t *t);

/* ---- interrupts (single-threaded sim: timers never preempt) -------------- */
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }

/* ---- GPIO ------------------------------------------------------------------ */
#define GPIO_OUT 1
#define GPIO_IN  0
enum gpio_function { GPIO_FUNC_SPI = 1, GPIO_FUNC_UART = 2, GPIO_FUNC_I2C = 3, GPIO_FUNC_PWM = 4, GPIO_FUNC_SIO = 5 };
void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_put(uint gpio, bool value);
void gpio_pull_up(uint gpio);
void gpio_disable_pulls(uint gpio);
bool gpio_get(uint gpio);
void gpio_set_function(uint gpio, enum gpio_function fn);
uint32_t gpio_get_all(void);

/* ---- SPI (ST7789 emulator behind it) ------------------------------------- */
typedef struct spi_inst spi_inst_t;
#define spi0 ((spi_inst_t *)(uintptr_t)1)
#define spi1 ((spi_inst_t *)(uintptr_t)2)
typedef enum { SPI_CPOL_0 = 0, SPI_CPOL_1 = 1 } spi_cpol_t;
typedef enum { SPI_CPHA_0 = 0, SPI_CPHA_1 = 1 } spi_cpha_t;
typedef enum { SPI_LSB_FIRST = 0, SPI_MSB_FIRST = 1 } spi_order_t;
uint spi_init(spi_inst_t *spi, uint baud);
void spi_set_format(spi_inst_t *spi, uint bits, spi_cpol_t cpol, spi_cpha_t cpha, spi_order_t order);
int  spi_write_blocking(spi_inst_t *spi, const uint8_t *src, size_t len);

/* ---- PWM (TFT backlight) ---------------------------------------------------- */
typedef struct { uint32_t csr, div, top; } pwm_config;
uint pwm_gpio_to_slice_num(uint gpio);
uint pwm_gpio_to_channel(uint gpio);
pwm_config pwm_get_default_config(void);
void pwm_config_set_clkdiv(pwm_config *c, float div);
void pwm_config_set_wrap(pwm_config *c, uint16_t wrap);
void pwm_init(uint slice, pwm_config *c, bool start);
void pwm_set_chan_level(uint slice, uint chan, uint16_t level);

/* ---- I2C (SSD1306 emulator behind it) ------------------------------------ */
typedef struct i2c_inst i2c_inst_t;
#define i2c0 ((i2c_inst_t *)(uintptr_t)1)
#define i2c1 ((i2c_inst_t *)(uintptr_t)2)
#define PICO_ERROR_GENERIC (-1)
uint i2c_init(i2c_inst_t *i2c, uint baud);
int  i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, uint timeout_us);
int  i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, uint timeout_us);

/* ---- USB stdio (the debug log: no host attached in the sim) -------------- */
bool stdio_init_all(void);
bool stdio_usb_connected(void);

/* ======================= simulator internals ============================== */
/* Display emulation */
#define SIM_DISP_TFT  2
#define SIM_DISP_OLED 1
void     sim_hal_reset(int oled_fitted);
uint8_t *sim_hal_fb(void);           /* RGBA */
int      sim_hal_fb_w(void);
int      sim_hal_fb_h(void);
uint32_t sim_hal_fb_version(void);
int      sim_hal_brightness(void);   /* 0..100 as the driver set it */
bool     sim_hal_display_on(void);
void     sim_hal_set_key(int idx, bool down);   /* 0 up 1 down 2 left 3 right 4 press */

/* RTC emulation (RV-3028 at 0x52): set before sim_hal_reset(). */
void     sim_hal_rtc_config(bool fitted, uint32_t secs_since_2000, bool porf);
uint32_t sim_hal_rtc_secs(void);
bool     sim_hal_rtc_valid(void);

/* Power / battery emulation. */
void     sim_hal_set_boot_keys(uint32_t idx_mask);  /* joystick contacts held at power-up (bit = 0 up .. 4 press) */
bool     sim_hal_kill_active(void);                 /* firmware drives PIN_PWR_KILL to "off" */
bool     sim_hal_relays(void);                      /* bypass relays energised (GP2 high) */
void     sim_hal_power_cut(void);                   /* supply gone: timers stop, panel dark */
void     sim_hal_batt_config(bool fitted, double soc_pct, bool usb);
void     sim_hal_batt_set(double soc_pct);
void     sim_hal_batt_usb(bool usb);
double   sim_hal_batt_soc(void);

/* Scheduler: run timers due at or before t. */
void     sim_hal_run_timers(uint64_t t);
uint64_t sim_hal_next_timer(void);
/* Advance time to t, running core1 and timers (provided by sim_io.c). */
void     sim_advance_to(uint64_t t);
void     sim_set_time(uint64_t t);

#endif /* MIDIUM_SIM_HAL_H */
