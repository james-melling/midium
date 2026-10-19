/* log_flash_rp2350.h - QSPI flash backend for the log store (see the .c file
 * for why flash writes are safe while core1 runs the thru path). */
#ifndef MIDIUM_LOG_FLASH_RP2350_H
#define MIDIUM_LOG_FLASH_RP2350_H
#include "log_store.h"
/* NULL if the firmware image overlaps the log region. */
const log_flash_t *log_flash_rp2350(void);
#endif
