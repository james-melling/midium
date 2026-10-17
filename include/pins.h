/*
 * pins.h - MIDIum (NSK MT-1) GPIO map, Raspberry Pi Pico 2 (RP2350) prototype.
 *
 * This is the single source of truth for pin assignments. docs/wiring.md
 * documents the same map for the breadboard; keep the two in sync.
 */
#ifndef MIDIUM_PINS_H
#define MIDIUM_PINS_H

/* ---- MIDI ------------------------------------------------------------- */
#define PIN_MIDI_OUT_A_TX    0   /* UART0 TX -> MIDI OUT A driver (via bypass relays) */
#define PIN_MIDI_IN_RX       1   /* UART0 RX <- MIDI IN (H11L1 opto output)          */
#define PIN_BYPASS_RELAY     2   /* HIGH = relays energised = MCU owns IN and OUT A  */
#define PIN_MIDI_OUT_B_TX    4   /* UART1 TX -> MIDI OUT B driver                    */

#define MIDI_UART_A          uart0   /* RX = MIDI IN, TX = OUT A */
#define MIDI_UART_B          uart1   /* TX = OUT B (RX unused)   */
#define MIDI_BAUD            31250

/* ---- I2C1: OLED (0x3C); later RV-3028 RTC + MAX17048 fuel gauge --------- */
#define PIN_I2C_SDA          6
#define PIN_I2C_SCL          7
#define DISPLAY_I2C          i2c1
#define I2C_BAUD             400000
#define OLED_I2C_ADDR        0x3C
/* Reserved for milestone 2 (not implemented yet):
 *   RV-3028-C7 RTC          0x52
 *   MAX17048 fuel gauge     0x36                                           */

/* ---- SPI0: ST7789 1.9" TFT 170x320 (used as 320x170 landscape) --------- */
#define PIN_TFT_DC           16
#define PIN_TFT_CS           17
#define PIN_TFT_SCK          18
#define PIN_TFT_MOSI         19
#define PIN_TFT_RST          20
#define PIN_TFT_BL           21  /* backlight, PWM (slice 2 channel B) */
#define TFT_SPI              spi0

/* ---- SPI1: microSD (reserved for milestone 2, not used yet) ------------ */
#define PIN_SD_SCK           10
#define PIN_SD_MOSI          11
#define PIN_SD_MISO          12
#define PIN_SD_CS            13

/* ---- 5-way joystick, active low, internal pull-ups --------------------- */
#define PIN_JOY_UP           8
#define PIN_JOY_DOWN         9
#define PIN_JOY_LEFT         14
#define PIN_JOY_RIGHT        15
#define PIN_JOY_PRESS        22

/* ---- Misc --------------------------------------------------------------- */
#define PIN_LED_ACTIVITY     25  /* onboard LED = MIDI activity blink */

/* Reserved for XRC / GPI (milestone 3): GP26, GP27, GP28 */
#define PIN_RESERVED_XRC0    26
#define PIN_RESERVED_XRC1    27
#define PIN_RESERVED_XRC2    28

#endif /* MIDIUM_PINS_H */
