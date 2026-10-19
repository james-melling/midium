/*
 * logger.c - message log sessions on core0. See logger.h.
 */
#include "pico/stdlib.h"

#include "logger.h"
#include "clock.h"
#include "midi_io.h"
#include "generator.h"

static log_t s_log;

bool logger_time(uint64_t t_us, log_time_t *t) {
    t->t_us = t_us;
    t->real_s = 0;
    t->real_ms = 0;
    t->real = clock_real(t_us, &t->real_s, &t->real_ms);
    return t->real;
}

void logger_new_session(void) {
    log_time_t t;
    logger_time(time_us_64(), &t);
    log_new_session(&s_log, &t);
}

void logger_init(const log_flash_t *fl) {
    log_init(&s_log, fl);
    logger_new_session();
}

void logger_msg(const midi_msg_t *m) {
    log_time_t t;
    logger_time(m->t_us, &t);
    log_append(&s_log, m, &t);
}

void logger_task(uint64_t now) {
    if (!s_log.enabled) return;
    if (log_wiping(&s_log, NULL)) {
        log_service(&s_log, true);
        if (!log_wiping(&s_log, NULL)) logger_new_session();
        return;
    }
    uint64_t first;
    if (log_open_page(&s_log, &first) && now > first && now - first >= LOGGER_FLUSH_US) log_flush(&s_log);
    uint64_t last_rx = (uint64_t)midi_io_last_rx_ms() * 1000u;
    bool quiet = now > last_rx && now - last_rx >= LOGGER_QUIET_US;
    log_service(&s_log, quiet && !gen_clock_running());
}

void logger_flush_now(void) {
    log_flush(&s_log);
    for (int i = 0; i < 64 && log_pending(&s_log); i++) log_service(&s_log, true);
}

void logger_delete_all(void) { log_wipe_start(&s_log); }
log_t *logger_log(void) { return &s_log; }
bool logger_enabled(void) { return s_log.enabled; }
