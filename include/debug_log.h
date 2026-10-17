/*
 * debug_log.h - USB CDC debug log (pico stdio over USB).
 *
 * Isolated on purpose: milestone 3 replaces the CDC-only USB stack with a
 * composite USB-MIDI + mass-storage device. Only this module (and the
 * pico_enable_stdio_usb line in CMakeLists.txt) should need to change.
 */
#ifndef MIDIUM_DEBUG_LOG_H
#define MIDIUM_DEBUG_LOG_H

#include "midi_parser.h"

void dbg_init(void);
void dbg_log_msg(const midi_msg_t *m);
void dbg_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* MIDIUM_DEBUG_LOG_H */
