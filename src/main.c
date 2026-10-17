/*
 * MIDIum (NSK MT-1) - inline MIDI test / thru box.
 * Milestone 1 firmware: thru + merge engine, monitor, generator, menus.
 *
 * core1: MIDI engine only (src/midi_io.c, src/midi_engine.c).
 * core0: everything else - display, joystick, parser/monitor, generator, USB.
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
    con_row(1, CLS_HEADER, "  " MIDIUM_NAME);
    con_row(2, CLS_DIM, "  " MIDIUM_MODEL " v" MIDIUM_VERSION);
    con_row(4, CLS_NORMAL, "  inline MIDI tester");
    con_flush_all();
}

int main(void) {
    /* 1. Bypass relays OFF first: IN stays hard-wired to OUT A. */
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
    sleep_ms(700);
    con_clear();

    dbg_printf("\n%s %s v%s, display: %s\n", MIDIUM_NAME, MIDIUM_MODEL, MIDIUM_VERSION, drv->name);

    for (;;) {
        uint64_t now = time_us_64();
        joy_event_t e;
        while ((e = joystick_get()) != JOY_NONE) ui_event(e);
        bool changed = monitor_poll(512);
        gen_task(now);
        ui_render(now, changed);
        con_flush_step();
    }
}
