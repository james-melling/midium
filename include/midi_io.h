/*
 * midi_io.h - RP2350 glue for the MIDI engine: UART setup, bypass relay and
 * the core1 polling loop.
 */
#ifndef MIDIUM_MIDI_IO_H
#define MIDIUM_MIDI_IO_H

#include "midi_engine.h"

/* Shared engine instance (core1 owns the thru side, core0 the queues). */
extern me_t g_engine;

/* Configure UART0/UART1 at 31250 baud with TX idle high. Call first. */
void midi_io_init(me_settings_t *settings);

/* Energise the bypass relays (GP2 HIGH): MCU takes over MIDI IN A and OUT A. */
void midi_io_engage_relays(void);

/* Launch the engine loop on core1. Also starts the watchdog, fed by core1:
 * if the thru loop ever stops, the chip resets and the relays drop out to
 * hard-wired bypass. */
void midi_io_start_core1(void);

/* Time of the most recent input byte (ms since boot), for activity display. */
uint32_t midi_io_last_rx_ms(void);

/* True if the previous reset was caused by the core1 watchdog. */
bool midi_io_watchdog_reset(void);

#endif /* MIDIUM_MIDI_IO_H */
