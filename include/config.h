/*
 * config.h - build-time configuration for MIDIum.
 * Most of these can be overridden from CMake (-D...) or the compiler line.
 */
#ifndef MIDIUM_CONFIG_H
#define MIDIUM_CONFIG_H

#ifndef MIDIUM_VERSION
#define MIDIUM_VERSION "0.6.0"
#endif

#define MIDIUM_NAME   "MIDIum"
#define MIDIUM_MODEL  "NSK MT-1"

/* Display selection: 0 = auto (probe OLED at I2C 0x3C/0x3D, else TFT),
 *                    1 = force OLED, 2 = force TFT. */
#define DISPLAY_AUTO 0
#define DISPLAY_OLED 1
#define DISPLAY_TFT  2
#ifndef MIDIUM_DISPLAY
#define MIDIUM_DISPLAY DISPLAY_AUTO
#endif

/* OLED controller: MIDIUM_OLED_SSD1306=1 (default, Adafruit 938) or 0 for SH1106. */
#ifndef MIDIUM_OLED_SSD1306
#define MIDIUM_OLED_SSD1306 1
#endif

/* ---- TFT module (ST7789) ----------------------------------------------
 * MIDIUM_TFT_MODULE picks the wiring and, unless MIDIUM_TFT_PANEL says
 * otherwise, the panel:
 *   TFT_MODULE_PIMORONI_28  Pimoroni Pico Display Pack 2.8" (320x240 IPS,
 *                           ST7789V), jumpered to the breadboard Pico: DC GP16,
 *                           CS GP17, SCK GP18, MOSI GP19, backlight GP20, no
 *                           reset pin (the pack ties it to RUN; software reset
 *                           at init). Default: the prototype.
 *   TFT_MODULE_GENERIC_28   generic 2.8" 240x320 ST7789 IPS breakout: DC GP16,
 *                           CS GP17, SCK GP18, MOSI GP19, RST GP20, BL GP21
 *                           (the final PCB pinout).
 *   TFT_MODULE_ADAFRUIT_19  Adafruit 5394 1.9" 170x320 ST7789 (v0.5 and
 *                           earlier): same pins as the generic module.
 * See docs/wiring.md. */
#define TFT_MODULE_PIMORONI_28 1
#define TFT_MODULE_GENERIC_28  2
#define TFT_MODULE_ADAFRUIT_19 3
#ifndef MIDIUM_TFT_MODULE
#if defined(MIDIUM_TFT_PANEL) && MIDIUM_TFT_PANEL == 19
#define MIDIUM_TFT_MODULE TFT_MODULE_ADAFRUIT_19
#else
#define MIDIUM_TFT_MODULE TFT_MODULE_PIMORONI_28
#endif
#endif

/* Panel: 28 = 2.8" 240x320 (used landscape 320x240, 40 x 16 text cells),
 *        19 = 1.9" 170x320 (landscape 320x170, 40 x 11 text cells). */
#ifndef MIDIUM_TFT_PANEL
#if MIDIUM_TFT_MODULE == TFT_MODULE_ADAFRUIT_19
#define MIDIUM_TFT_PANEL 19
#else
#define MIDIUM_TFT_PANEL 28
#endif
#endif

#if MIDIUM_TFT_PANEL == 28
/* 2.8" 240x320: the panel fills the controller's 240 x 320 RAM, so no
 * offsets. Landscape = MADCTL MV (row/column exchange) | MX = 0x60. */
#ifndef TFT_WIDTH
#define TFT_WIDTH     320
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT    240
#endif
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET  0
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET  0
#endif
#elif MIDIUM_TFT_PANEL == 19
/* 1.9" 170x320: in portrait the 170-px-wide panel sits at controller column
 * 35 of 240. Rotated to landscape (MADCTL MV) rows and columns swap: the 35
 * becomes the ROW (y) offset, x offset is 0. */
#ifndef TFT_WIDTH
#define TFT_WIDTH     320
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT    170
#endif
#ifndef TFT_X_OFFSET
#define TFT_X_OFFSET  0
#endif
#ifndef TFT_Y_OFFSET
#define TFT_Y_OFFSET  35
#endif
#else
#error "MIDIUM_TFT_PANEL must be 28 or 19"
#endif

/* MADCTL for landscape: MV | MX = 0x60 (RGB order). Use 0xA0 (MV | MY) to
 * rotate 180 degrees; add 0x08 (BGR) if red and blue are swapped on your
 * panel. */
#ifndef TFT_MADCTL
#define TFT_MADCTL    0x60
#endif
/* 1 = send INVON at init. IPS ST7789 panels (all three modules above) need
 * it, or every colour shows inverted (black background looks white). */
#ifndef TFT_INVERT
#define TFT_INVERT    1
#endif
#ifndef TFT_SPI_HZ
#define TFT_SPI_HZ    62500000   /* RP2350 @150 MHz gives 37.5 MHz (nearest legal divider) */
#endif

/* Joystick timing */
#define JOY_DEBOUNCE_MS     10
#define JOY_REPEAT_DELAY_MS 400
#define JOY_REPEAT_RATE_MS  80

/* Monitor */
#define HISTORY_LEN 128

/* Message log in the Pico 2's 4 MB QSPI flash: the top 3 MB (sector
 * aligned). The firmware image must stay below LOG_FLASH_OFFSET (checked at
 * boot: logging is disabled if it ever grows into the region). */
#ifndef LOG_FLASH_OFFSET
#define LOG_FLASH_OFFSET (1u * 1024u * 1024u)
#endif
#ifndef LOG_FLASH_SIZE
#define LOG_FLASH_SIZE   (3u * 1024u * 1024u)
#endif

/* ---- Soft power (see README "Power") ------------------------------------
 * The joystick centre switch is also the power button: hold it
 * PWR_HOLD_MS for the power-off countdown, PWR_COUNTDOWN_MS more to power off. */
#ifndef PWR_HOLD_MS
#define PWR_HOLD_MS        2000u
#endif
#ifndef PWR_COUNTDOWN_MS
#define PWR_COUNTDOWN_MS   2000u
#endif
/* PIN_PWR_KILL: 1 = drive LOW to power off, HIGH to stay on (LTC2955 KILL,
 * MAX16150 CLR, or a P-FET hold circuit). 0 = the opposite polarity. */
#ifndef PWR_KILL_ACTIVE_LOW
#define PWR_KILL_ACTIVE_LOW 1
#endif
/* After driving PIN_PWR_KILL, if the MCU is still running this much later,
 * there is no controller (prototype, or USB keeps it alive): soft-off idle. */
#ifndef PWR_KILL_WAIT_MS
#define PWR_KILL_WAIT_MS   1000u
#endif
/* 1 = the push-button power controller is fitted (final PCB). Enables the
 * "USB power-on: Charge only" boot path (a power-up caused by USB, not the
 * button, goes straight to a charging screen). 0 = breadboard prototype. */
#ifndef MIDIUM_PWR_CTRL
#define MIDIUM_PWR_CTRL    0
#endif

/* ---- Battery (MAX17048 gauge; no gauge = no battery UI) ----------------- */
#define BATT_POLL_MS       1000u   /* gauge read interval */
#ifndef BATT_WARN_PCT
#define BATT_WARN_PCT      10      /* header warning at or below */
#endif
#ifndef BATT_POPUP_PCT
#define BATT_POPUP_PCT     5       /* one-time popup at or below */
#endif
#ifndef BATT_OFF_PCT
#define BATT_OFF_PCT       3       /* clean shutdown at or below ... */
#endif
#ifndef BATT_OFF_MV
#define BATT_OFF_MV        3300    /* ... or when the cell is below this */
#endif
#define BATT_OFF_SAMPLES   3       /* for this many polls in a row (load transients) */

#endif /* MIDIUM_CONFIG_H */
