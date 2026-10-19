/*
 * log_flash_rp2350.c - log store backend on the Pico 2's QSPI flash.
 *
 * The log region is the top LOG_FLASH_SIZE bytes below the end of the
 * 4 MB part (config.h). Reads go through XIP (memory mapped). Erase and
 * program use the SDK's flash_range_erase / flash_range_program.
 *
 * Why this is safe for the thru path (core1)
 * ------------------------------------------
 * While the flash is being erased or programmed it cannot be read, so XIP
 * (execute-in-place) stalls/faults for any bus master that touches it. The
 * usual SDK answer, flash_safe_execute() / multicore_lockout, parks the other
 * core for the duration. That would stop MIDI thru for up to ~400 ms per
 * sector erase, which an inline show-control box must never do. Instead:
 *
 *  1. core1 never touches flash. core1_main and everything it calls
 *     (me_input_byte, pump, me_service, me_out_pop, me_mon_push, and the
 *     64-bit divide helper) are linked into SRAM (.time_critical.*,
 *     __not_in_flash_func). Its data (g_engine, the routing matrix, its
 *     stack in SCRATCH_X) is all in SRAM, and the engine has no const tables
 *     (no .rodata); README "Thru / merge engine" lists the objdump check,
 *     which finds no call or literal into 0x10xxxxxx from any core1 function.
 *  2. core1 takes no interrupts: it is a polling loop (UART FIFOs, timer
 *     registers, SIO GPIO, watchdog load register), all peripheral accesses.
 *     No IRQ is enabled on core1, so no vector fetch or handler can pull
 *     code from flash, and nothing core1 waits on is serviced by an IRQ.
 *  3. core0 is the only flash user. For each erase / program it disables
 *     its own interrupts (save_and_disable_interrupts, core0 only), so none
 *     of its handlers (joystick timer, clock generator, USB) can run from
 *     flash mid-operation, and calls the SDK routines, which run from RAM
 *     and restore XIP (and flush the XIP cache) before returning. The
 *     wrappers below are themselves in RAM.
 *  4. Nothing else masters the flash (no DMA from flash).
 *
 * Effects on core0 only: the UI pauses for the duration (~1 ms per page,
 * ~45 ms typical per sector erase), the monitor ring (4096 entries, over
 * 0.6 s of both inputs at full rate) buffers input meanwhile, and the clock
 * generator's timer IRQ can be up to that late (erases are therefore only
 * done while the clock generator is off, unless the log queue is backing up).
 * The core1 watchdog is fed by core1, so it is unaffected.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"

#include "config.h"
#include "log_store.h"
#include "log_flash_rp2350.h"

#if LOG_FLASH_OFFSET % FLASH_SECTOR_SIZE != 0 || LOG_FLASH_SIZE % FLASH_SECTOR_SIZE != 0
#error "log region must be sector aligned"
#endif
#if defined(PICO_FLASH_SIZE_BYTES) && (LOG_FLASH_OFFSET + LOG_FLASH_SIZE > PICO_FLASH_SIZE_BYTES)
#error "log region is beyond the end of flash"
#endif

extern char __flash_binary_end;

static void f_read(void *ctx, uint32_t off, void *dst, uint32_t len) {
    (void)ctx;
    memcpy(dst, (const void *)(XIP_BASE + LOG_FLASH_OFFSET + off), len);
}

static bool __not_in_flash_func(f_erase)(void *ctx, uint32_t off) {
    (void)ctx;
    if (off % FLASH_SECTOR_SIZE || off >= LOG_FLASH_SIZE) return false;
    uint32_t irq = save_and_disable_interrupts();   /* core0 only; core1 keeps running from SRAM */
    flash_range_erase(LOG_FLASH_OFFSET + off, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
    return true;
}

static bool __not_in_flash_func(f_program)(void *ctx, uint32_t off, const uint8_t *src) {
    (void)ctx;
    if (off % FLASH_PAGE_SIZE || off >= LOG_FLASH_SIZE) return false;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(LOG_FLASH_OFFSET + off, src, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
    return true;
}

static const log_flash_t s_flash = {
    .size = LOG_FLASH_SIZE,
    .ctx = NULL,
    .read = f_read,
    .erase_sector = f_erase,
    .program_page = f_program,
};

const log_flash_t *log_flash_rp2350(void) {
    /* Refuse if the firmware image ever grows into the log region. */
    if ((uintptr_t)&__flash_binary_end - XIP_BASE > LOG_FLASH_OFFSET) return NULL;
    return &s_flash;
}
