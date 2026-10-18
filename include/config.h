/*
 * config.h - build-time configuration for MIDIum.
 * Most of these can be overridden from CMake (-D...) or the compiler line.
 */
#ifndef MIDIUM_CONFIG_H
#define MIDIUM_CONFIG_H

#ifndef MIDIUM_VERSION
#define MIDIUM_VERSION "0.3.0"
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

#endif /* MIDIUM_CONFIG_H */
