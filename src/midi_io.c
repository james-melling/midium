/*
 * midi_io.c - RP2350 glue for the MIDI thru/merge engine.
 *
 * core1 runs a tight polling loop from SRAM:
 *   UART0 RX (IN A) --+
 *                     +-> me_input_byte() -> merger -> per-output TX queues -> UART0/UART1 TX
 *   UART1 RX (IN B) --+
 * and copies every input byte (with a timestamp and its input) into the
 * monitor ring for core0. No display, USB or flash work happens on core1.
 *
 * TX pacing: a byte is written to a UART only when its TX FIFO is empty. The
 * shift register is still sending the previous byte at that point, so the
 * line stays 100% busy when there is a backlog, but at most one byte ever
 * waits in hardware. Everything else waits in the software queue, where
 * realtime bytes can still jump ahead (keeps clock jitter to < 1 byte time).
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/structs/timer.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/watchdog.h"

#include "pins.h"
#include "midi_io.h"

me_t g_engine;

static volatile uint32_t s_last_rx_ms;
static bool s_wdt_reset;

#define WATCHDOG_MS     250u
#define LED_HOLD_US     30000u
#if PICO_RP2040
#define WDT_LOAD        (WATCHDOG_MS * 1000u * 2u)   /* RP2040-E1 erratum */
#else
#define WDT_LOAD        (WATCHDOG_MS * 1000u)
#endif

static inline uint64_t __not_in_flash_func(now_us)(void) {
    /* Read the 64-bit timer without latching (safe from either core). */
    uint32_t hi = timer_hw->timerawh;
    uint32_t lo;
    for (;;) {
        lo = timer_hw->timerawl;
        uint32_t hi2 = timer_hw->timerawh;
        if (hi == hi2) break;
        hi = hi2;
    }
    return ((uint64_t)hi << 32) | lo;
}

static void uart_setup(uart_inst_t *u, uint tx_pin, int rx_pin) {
    uart_init(u, MIDI_BAUD);
    uart_set_format(u, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(u, false, false);
    uart_set_fifo_enabled(u, true);
    uart_set_translate_crlf(u, false);
    /* UART is enabled before the pins are switched over, so TX is already
     * idle-high when the pin connects: no glitch start bit downstream. */
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    if (rx_pin >= 0) {
        gpio_pull_up((uint)rx_pin);   /* idle high if the opto is absent */
        gpio_set_function((uint)rx_pin, GPIO_FUNC_UART);
    }
}

void midi_io_init(me_settings_t *settings) {
    s_wdt_reset = watchdog_caused_reboot();
    me_init(&g_engine, settings);
    uart_setup(MIDI_UART_A, PIN_MIDI_OUT_A_TX, PIN_MIDI_IN_A_RX);
    uart_setup(MIDI_UART_B, PIN_MIDI_OUT_B_TX, PIN_MIDI_IN_B_RX);

    gpio_init(PIN_LED_ACTIVITY);
    gpio_set_dir(PIN_LED_ACTIVITY, GPIO_OUT);
    gpio_put(PIN_LED_ACTIVITY, 0);
}

void midi_io_engage_relays(void) {
    /* Let the TX lines sit idle-high for a few byte times first so the
     * downstream receiver sees a clean idle line when the relay switches. */
    sleep_ms(5);
    gpio_put(PIN_BYPASS_RELAY, 1);
}

bool midi_io_watchdog_reset(void) { return s_wdt_reset; }
uint32_t midi_io_last_rx_ms(void) { return s_last_rx_ms; }

/* Drain one UART's RX FIFO into the engine. Inlined into core1_main. */
static inline bool __attribute__((always_inline)) rx_poll(me_t *me, uart_hw_t *u, int in, uint64_t *now) {
    bool got = false;
    while (!(u->fr & UART_UARTFR_RXFE_BITS)) {
        uint32_t dr = u->dr;
        *now = now_us();
        if (dr & (UART_UARTDR_OE_BITS | UART_UARTDR_BE_BITS | UART_UARTDR_FE_BITS)) {
            me->in[in].st.uart_errors++;
            uint8_t code = (dr & UART_UARTDR_BE_BITS) ? ME_ERR_BREAK
                         : (dr & UART_UARTDR_FE_BITS) ? ME_ERR_FRAMING
                         : ME_ERR_OVERRUN;
            me_mon_push(me, in, code, ME_MON_F_ERR, *now);
            /* Overrun: this byte is good, earlier ones were lost.
             * Framing / break: this byte is garbage, drop it. */
            if (code != ME_ERR_OVERRUN) continue;
        }
        uint8_t b = (uint8_t)dr;
        me_input_byte(me, in, b, *now);
        me_mon_push(me, in, b, 0, *now);
        got = true;
    }
    return got;
}

static void __not_in_flash_func(core1_main)(void) {
    uart_hw_t *const ua = uart_get_hw(MIDI_UART_A);
    uart_hw_t *const ub = uart_get_hw(MIDI_UART_B);
    me_t *const me = &g_engine;
    uint64_t led_off_at = 0;
    bool led_on = false;
    uint64_t next_wdt = 0;

    for (;;) {
        uint64_t now = now_us();

        /* ---- receive: IN A (UART0), IN B (UART1) ---- */
        bool got = rx_poll(me, ua, ME_IN_A, &now);
        got |= rx_poll(me, ub, ME_IN_B, &now);
        if (got) {
            s_last_rx_ms = (uint32_t)(now / 1000u);
            sio_hw->gpio_set = 1u << PIN_LED_ACTIVITY;
            led_on = true;
            led_off_at = now + LED_HOLD_US;
        }

        /* ---- merge generated traffic ---- */
        me_service(me, now);

        /* ---- transmit (one byte in hardware at a time, see header) ---- */
        uint8_t b;
        if ((ua->fr & UART_UARTFR_TXFE_BITS) && me_out_pop(me, ME_OUT_A, &b)) ua->dr = b;
        if ((ub->fr & UART_UARTFR_TXFE_BITS) && me_out_pop(me, ME_OUT_B, &b)) ub->dr = b;

        /* ---- housekeeping ---- */
        if (led_on && now >= led_off_at) {
            sio_hw->gpio_clr = 1u << PIN_LED_ACTIVITY;
            led_on = false;
        }
        if (now >= next_wdt) {
            /* Same register write as watchdog_update(), done inline so the
             * whole core1 loop runs from SRAM with no calls into flash
             * (matters once milestone 2 starts writing flash). */
            watchdog_hw->load = WDT_LOAD;
            next_wdt = now + 10000u;
        }
    }
}

void midi_io_start_core1(void) {
    multicore_launch_core1(core1_main);
    watchdog_enable(WATCHDOG_MS, true);
}
