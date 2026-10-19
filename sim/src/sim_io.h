/* sim_io.h - simulator MIDI wire model (see sim_io.c). */
#ifndef MIDIUM_SIM_IO_H
#define MIDIUM_SIM_IO_H
#include <stdint.h>
#include <stdbool.h>

bool     sim_io_rx_at(int in, uint8_t b, uint64_t t_us);
uint64_t sim_io_rx_free_at(int in);
uint32_t sim_io_rx_pending(int in);
void     sim_io_rx_flush(int in);
uint32_t sim_io_rx_total(int in);
int      sim_io_out_read(int port, uint8_t *buf, double *times, int max);
uint32_t sim_io_out_total(int port);
bool     sim_io_led(void);
bool     sim_io_reboot_requested(void);
void     sim_io_set_soft_wake(bool w);
void     sim_io_power_cut(void);
bool     sim_io_relays(void);

#endif
