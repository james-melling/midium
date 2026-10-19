/*
 * config.h - build-time configuration for MIDIum.
 * Most of these can be overridden from CMake (-D...) or the compiler line.
 */
#ifndef MIDIUM_CONFIG_H
#define MIDIUM_CONFIG_H

#ifndef MIDIUM_VERSION
#define MIDIUM_VERSION "0.5.0"
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

/* ST7789 1.9" 170x320 panel geometry in landscape (320 x 170).
 * The panel is 170 px wide in portrait, centred in the controller's 240-column
 * RAM, so portrait has a column offset of 35. Rotated to landscape (MADCTL MV)
 * rows and columns swap: the 35 becomes the ROW (y) offset, x offset is 0. */
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
/* MADCTL for landscape: MV | MX = 0x60 (RGB order). Use 0xA0 (MV | MY) to
 * rotate 180 degrees; add 0x08 if red and blue are swapped on your panel. */
#ifndef TFT_MADCTL
#define TFT_MADCTL    0x60
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
