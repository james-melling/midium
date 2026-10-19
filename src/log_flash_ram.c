/*
 * log_flash_ram.c - RAM emulation of NOR flash for the log store.
 */
#include <string.h>
#include "log_flash_ram.h"

static void r_read(void *ctx, uint32_t off, void *dst, uint32_t len) {
    log_ram_flash_t *r = ctx;
    if (off >= r->size) { memset(dst, 0xFF, len); return; }
    if (len > r->size - off) len = r->size - off;
    memcpy(dst, r->mem + off, len);
}

static bool r_erase(void *ctx, uint32_t off) {
    log_ram_flash_t *r = ctx;
    if (!r->powered || off % LOG_SECTOR_SIZE || off >= r->size) return false;
    memset(r->mem + off, 0xFF, LOG_SECTOR_SIZE);
    r->erases++;
    return true;
}

static bool r_program(void *ctx, uint32_t off, const uint8_t *src) {
    log_ram_flash_t *r = ctx;
    if (!r->powered || off % LOG_PAGE_SIZE || off >= r->size) return false;
    uint32_t n = LOG_PAGE_SIZE;
    if (r->fail_after_programs == 0) {
        n = r->fail_bytes < LOG_PAGE_SIZE ? r->fail_bytes : LOG_PAGE_SIZE;
        r->powered = false;
    }
    if (r->fail_after_programs > 0) r->fail_after_programs--;
    for (uint32_t i = 0; i < n; i++) r->mem[off + i] &= src[i];
    r->programs++;
    return r->powered;
}

void log_ram_flash_init(log_ram_flash_t *r, log_flash_t *fl, uint8_t *mem, uint32_t size, bool format) {
    memset(r, 0, sizeof *r);
    r->mem = mem;
    r->size = size;
    r->fail_after_programs = -1;
    r->powered = true;
    if (format) memset(mem, 0xFF, size);
    fl->size = size;
    fl->ctx = r;
    fl->read = r_read;
    fl->erase_sector = r_erase;
    fl->program_page = r_program;
}
