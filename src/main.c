/*
 * MIDIum MT-1 (melling.audio) - inline MIDI test / thru box.
 * Thru + merge engine (two inputs, routing matrix), monitor, generator, menus.
 *
 * core1: MIDI engine only (src/midi_io.c, src/midi_engine.c).
 * core0: everything else - display, joystick, parser/monitor, generator, USB,
 *        RTC, flash log.
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "config.h"
#include "settings.h"
#include "midi_io.h"
#include "display.h"
#include "joystick.h"
#include "monitor.h"
#include "generator.h"
#include "ui.h"
#include "debug_log.h"
#include "clock.h"
#include "logger.h"
#include "log_flash_rp2350.h"
#include "power.h"

static const display_driver_t *select_display(void) {
#if MIDIUM_DISPLAY == DISPLAY_OLED
    return &g_disp_oled;
#elif MIDIUM_DISPLAY == DISPLAY_TFT
    return &g_disp_tft;
#else
    return oled_probe() ? &g_disp_oled : &g_disp_tft;
#endif
}

static void splash(void) {
    con_clear();
    int r = con_rows() >= 16 ? con_rows() / 2 - 3 : 1;   /* centred on the 2.8" */
    con_row(r, CLS_HEADER, "  " MIDIUM_NAME);
    con_row(r + 1, CLS_DIM, "  " MIDIUM_MODEL "  v" MIDIUM_VERSION);
    con_row(r + 3, CLS_NORMAL, "  inline MIDI tester");
    con_row(r + 5, CLS_DIM, "  " MIDIUM_MAKER);
    con_flush_all();
}

int main(void) {
    /* 0. Hold the power on (soft power controller), note the power button. */
    power_early_init();

    /* 1. Bypass relays OFF first: IN A stays hard-wired to OUT A. */
    gpio_init(PIN_BYPASS_RELAY);
    gpio_put(PIN_BYPASS_RELAY, 0);
    gpio_set_dir(PIN_BYPASS_RELAY, GPIO_OUT);

    /* 2. MIDI UARTs up with TX idle-high, engine ready, then take the line. */
    settings_defaults(&g_settings);
    midi_io_init(&g_settings.route);
    midi_io_engage_relays();
    midi_io_start_core1();

    /* 3. Everything else on core0. */
    dbg_init();
    const display_driver_t *drv = select_display();
    if (!drv->init() && drv == &g_disp_oled) {
        drv = &g_disp_tft;
        drv->init();
    }
    con_init(drv);
    ui_init();
    splash();

    joystick_init();
    monitor_init();
    gen_init();
    /* Real-time clock (latches the RTC second: up to ~1.1 s, during the
     * splash), then the flash log and the boot session. */
    uint64_t t_splash = time_us_64();
    clock_init();
    /* Battery gauge; a power-up by USB alone may only charge (no logging). */
    bool charge_only = power_init();
    logger_init(charge_only ? NULL : log_flash_rp2350());
    uint64_t spent = time_us_64() - t_splash;
    if (spent < 700000u) sleep_us(700000u - spent);
    con_clear();

    dbg_printf("\n%s %s v%s (%s), display: %s\n", MIDIUM_NAME, MIDIUM_MODEL, MIDIUM_VERSION, MIDIUM_MAKER, drv->name);

    for (;;) {
        uint64_t now = time_us_64();
        joy_event_t e;
        while ((e = joystick_get()) != JOY_NONE) ui_event(e);
        bool changed = monitor_poll(512);
        gen_task(now);
        clock_task(now);
        logger_task(now);
        power_task(now);
        ui_render(now, changed);
        con_flush_step();
        /* soft-off idle with the panel dark: poll the button slowly */
        if (power_display_off()) sleep_ms(10);
    }
}
