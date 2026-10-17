/*
 * ui.h - joystick-driven UI: monitor view and menus (core0).
 */
#ifndef MIDIUM_UI_H
#define MIDIUM_UI_H

#include <stdint.h>
#include "joystick.h"

void ui_init(void);
void ui_event(joy_event_t e);
/* Re-render into the console if needed (cheap: the console only marks rows
 * that actually changed). */
void ui_render(uint64_t now_us, bool history_changed);

#endif /* MIDIUM_UI_H */
