/*
 * rv3028.h - Micro Crystal RV-3028-C7 real-time clock on I2C1 (shared with
 * the OLED), 7-bit address 0x52.
 *
 * Registers used (datasheet / application manual):
 *   00 Seconds  01 Minutes  02 Hours  03 Weekday  04 Date  05 Month  06 Year
 *      (BCD; hours in 24-hour mode; year 00-99 = 2000-2099)
 *   0E Status    bit 0 PORF: voltage dropped too low, time is invalid
 *                bit 5 BSF: switched to the backup supply; bit 7 EEbusy
 *   0F Control 1 bit 3 EERD: 1 = stop the automatic EEPROM refresh
 *   10 Control 2 bit 1 12_24: 0 = 24-hour mode
 *   27 EECMD     0x00 then 0x11 = copy all configuration RAM to EEPROM
 *   37 EEPROM Backup (RAM mirror of EEPROM 37h):
 *                bits 3:2 BSM backup switchover: 00 off (factory), 01 direct
 *                switching (DSM), 11 level switching (LSM); bit 4 FEDE;
 *                bit 5 TCE trickle charge (left as it is: never charge a
 *                primary cell)
 *
 * Writing the Seconds register resets the prescaler, so a new time starts
 * on a whole second.
 */
#ifndef MIDIUM_RV3028_H
#define MIDIUM_RV3028_H

#include <stdint.h>
#include <stdbool.h>
#include "timeutil.h"
#include "pins.h"

#define RV3028_I2C_ADDR   RTC_I2C_ADDR
#define RV3028_SECONDS    0x00
#define RV3028_STATUS     0x0E
#define RV3028_CTRL1      0x0F
#define RV3028_CTRL2      0x10
#define RV3028_EECMD      0x27
#define RV3028_EE_BACKUP  0x37
#define RV3028_ST_PORF    0x01u
#define RV3028_ST_BSF     0x20u
#define RV3028_ST_EEBUSY  0x80u
#define RV3028_C1_EERD    0x08u
#define RV3028_C2_12_24   0x02u
#define RV3028_BSM_MASK   0x0Cu
#define RV3028_BSM_DSM    0x04u

/* Probe 0x52; on success put the chip in 24-hour mode and enable direct
 * switching backup mode (written to EEPROM once if it differs). */
bool rv3028_init(void);
bool rv3028_present(void);
/* PORF: the clock lost power and its time is not valid. */
bool rv3028_time_lost(void);
/* Seconds register only (cheap, for finding the second boundary). */
bool rv3028_read_seconds(uint8_t *sec);
bool rv3028_read(tu_datetime_t *d);
/* Set the time and clear PORF. */
bool rv3028_write(const tu_datetime_t *d);
uint8_t rv3028_backup_mode(void);   /* BSM bits as read at init */

#endif
