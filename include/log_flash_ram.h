/*
 * log_flash_ram.h - NOR flash emulated in RAM for the log store (browser
 * simulator and host tests). Same semantics as the real part: erase sets a
 * 4 kB sector to 0xFF, programming can only clear bits (new = old AND data),
 * pages are 256 bytes. Tests can cut a program short to emulate power loss.
 */
#ifndef MIDIUM_LOG_FLASH_RAM_H
#define MIDIUM_LOG_FLASH_RAM_H

#include <stdint.h>
#include <stdbool.h>
#include "log_store.h"

typedef struct {
    uint8_t *mem;
    uint32_t size;
    /* fault injection: when fail_after_programs reaches 0, the next program
     * writes only fail_bytes bytes of its page and then "power is lost"
     * (powered = false: every later erase / program is ignored). */
    int32_t  fail_after_programs;   /* -1 = never */
    uint32_t fail_bytes;
    bool     powered;
    uint32_t programs, erases;
} log_ram_flash_t;

/* mem must hold size bytes; format = fill with 0xFF (a fresh chip). */
void log_ram_flash_init(log_ram_flash_t *r, log_flash_t *fl, uint8_t *mem, uint32_t size, bool format);

#endif
