/*
 * sim_tft19.c - the real TFT driver built a second time for the 1.9" panel
 * (MIDIUM_TFT_PANEL 19, the Adafruit 5394 wiring), so the simulator can
 * offer both panels from one build. The firmware picks one at compile time.
 */
#undef MIDIUM_TFT_MODULE
#define MIDIUM_TFT_MODULE TFT_MODULE_ADAFRUIT_19
#define g_disp_tft g_disp_tft19
#include "../../src/disp_tft.c"
