/*
 * pins.h - MIDIum (NSK MT-1) GPIO map, Raspberry Pi Pico 2 (RP2350) prototype.
 *
 * This is the single source of truth for pin assignments. docs/wiring.md
 * documents the same map for the breadboard; keep the two in sync.
 */
#ifndef MIDIUM_PINS_H
#define MIDIUM_PINS_H

#include "config.h"   /* MIDIUM_TFT_MODULE */

/* ---- MIDI ------------------------------------------------------------- */
#define PIN_MIDI_OUT_A_TX    0   /* UART0 TX -> MIDI OUT A driver (via bypass relays)  */
#define PIN_MIDI_IN_A_RX     1   /* UART0 RX <- MIDI IN A (U1 H11L1, via bypass relays) */
#define PIN_BYPASS_RELAY     2   /* HIGH = relays energised = MCU owns IN A and OUT A  */
#define PIN_PWR_KILL         3   /* soft power: KILL / HOLD line to the push-button on/off
                                    controller (LTC2955 / MAX16150 class). Polarity:
                                    PWR_KILL_ACTIVE_LOW in config.h. Not connected on the
                                    breadboard prototype.                              */
#define PIN_MIDI_OUT_B_TX    4   /* UART1 TX -> MIDI OUT B driver                      */
#define PIN_MIDI_IN_B_RX     5   /* UART1 RX <- MIDI IN B (U2 H11L1, no bypass)        */

#define MIDI_UART_A          uart0   /* RX = MIDI IN A, TX = MIDI OUT A */
#define MIDI_UART_B          uart1   /* RX = MIDI IN B, TX = MIDI OUT B */
#define MIDI_BAUD            31250

/* ---- I2C1: OLED (0x3C), RV-3028-C7 RTC (0x52), MAX17048 gauge (0x36) --- */
#define PIN_I2C_SDA          6
#define PIN_I2C_SCL          7
#define DISPLAY_I2C          i2c1
#define I2C_BAUD             400000
#define OLED_I2C_ADDR        0x3C
#define OLED_I2C_ADDR_ALT    0x3D   /* probed second (Adafruit 938 with DC high) */
#define RTC_I2C_ADDR         0x52   /* RV-3028-C7 (src/rv3028.c), same bus as the OLED */
#define GAUGE_I2C_ADDR       0x36   /* MAX17048 fuel gauge (src/max17048.c); optional */

/* ---- SPI0: ST7789 TFT, landscape (config.h MIDIUM_TFT_MODULE) ---------- */
#define PIN_TFT_DC           16
#define PIN_TFT_CS           17
#define PIN_TFT_SCK          18
#define PIN_TFT_MOSI         19
#if MIDIUM_TFT_MODULE == TFT_MODULE_PIMORONI_28
/* Pimoroni Pico Display Pack 2.8": backlight on GP20, reset tied to RUN */
#define PIN_TFT_RST          (-1)
#define PIN_TFT_BL           20  /* backlight, PWM (slice 2 channel A) */
#else
/* generic 2.8" breakout / Adafruit 1.9": separate reset, backlight on GP21 */
#define PIN_TFT_RST          20
#define PIN_TFT_BL           21  /* backlight, PWM (slice 2 channel B) */
#endif
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

/* USB power present. The Pico 2 board routes VBUS through a divider to GP24
 * (the SDK's PICO_VBUS_PIN): high while USB supplies 5 V. -1 = no sense pin
 * (then the firmware assumes battery power whenever a gauge answers). On the
 * final PCB route the USB-C VBUS detect here too. */
#ifndef PIN_VBUS_SENSE
#define PIN_VBUS_SENSE       24
#endif
/* Charger status (open-drain CHG / STAT, low = charging). Reserved for the
 * final PCB (GP23 is free there; on the Pico 2 board it drives the SMPS mode
 * pin, so it is NOT used on the prototype). -1 = not fitted: "charging" is
 * then inferred from VBUS present and the gauge below 100 %. */
#ifndef PIN_CHG_STAT
#define PIN_CHG_STAT         (-1)
#endif

/* Reserved for XRC / GPI (milestone 3): GP26, GP27, GP28 */
#define PIN_RESERVED_XRC0    26
#define PIN_RESERVED_XRC1    27
#define PIN_RESERVED_XRC2    28

#endif /* MIDIUM_PINS_H */
