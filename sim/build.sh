#!/usr/bin/env bash
# Build the MIDIum browser simulator: the real firmware core0 modules + engine,
# compiled to WebAssembly with the HAL shim in sim/, then inlined into
# sim/web/index.html (one self-contained page).
#
#   source ~/emsdk/emsdk_env.sh   # or set EMSDK
#   sim/build.sh
set -euo pipefail
cd "$(dirname "$0")"
SIM=$(pwd)
ROOT=$(cd .. && pwd)
OUT="$SIM/out"
mkdir -p "$OUT"

if ! command -v emcc >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1 || source /home/claude/emsdk/emsdk_env.sh >/dev/null
fi

# Firmware sources, unmodified. midi_io.c and main.c are replaced by
# sim_io.c / sim_main.c, and the QSPI flash log backend (log_flash_rp2350.c)
# by the RAM NOR emulation (log_flash_ram.c); everything else is the real code.
FW="midi_engine.c midi_parser.c midi_decode.c msc_decode.c settings.c monitor.c
    generator.c console.c ui.c joystick.c disp_tft.c tft_render.c mon_view.c disp_oled.c debug_log.c
    timeutil.c mtc.c msg_filter.c valedit.c bigdigits.c log_store.c log_flash_ram.c
    logger.c rv3028.c clock.c max17048.c power_fsm.c power.c"
SRCS=""
for f in $FW; do SRCS="$SRCS $ROOT/src/$f"; done
SRCS="$SRCS $SIM/src/sim_hal.c $SIM/src/sim_io.c $SIM/src/sim_main.c $SIM/src/sim_tft19.c"

VERSION=$(sed -n 's/^#define MIDIUM_VERSION "\(.*\)"/\1/p' "$ROOT/include/config.h")

EXPORTS='["_sim_init","_sim_tick","_sim_key","_sim_midi_in","_sim_midi_in_at","_sim_rx_free_at",
"_sim_rx_pending","_sim_rx_flush","_sim_out_read","_sim_now_us","_sim_framebuffer","_sim_fb_w",
"_sim_fb_h","_sim_fb_version","_sim_brightness","_sim_display_on","_sim_led","_sim_display_name",
"_sim_route_bits","_sim_clock_running","_sim_stat","_sim_rtc_config","_sim_rtc_now","_sim_rtc_valid",
"_sim_flash_ptr","_sim_flash_size","_sim_flash_restored","_sim_log_sessions","_sim_log_used_pct",
"_sim_boot_keys","_sim_soft_wake","_sim_power_kill","_sim_power_state","_sim_relays","_sim_power_cut",
"_sim_reboot_requested","_sim_batt_config","_sim_batt_set","_sim_batt_usb","_sim_batt_soc","_sim_idle_skip","_sim_autooff_left",
"_malloc","_free"]'
EXPORTS=$(echo "$EXPORTS" | tr -d '\n')

# Same warning set as the firmware build (CMakeLists.txt), as errors. clang adds
# -Wmissing-format-attribute and -Wformat-nonliteral (on vsnprintf wrappers) under
# -Wformat=2, which gcc does not; the firmware is built with gcc, so those two
# clang-only diagnostics are turned off.
emcc -std=c11 -O2 \
    -Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2 -Wno-missing-format-attribute -Wno-format-nonliteral -Werror \
    -DMIDIUM_VERSION="\"$VERSION\"" -DMIDIUM_DISPLAY=0 -DMIDIUM_OLED_SSD1306=1 -DMIDIUM_SIM=1 -DMIDIUM_PWR_CTRL=1 \
    -I"$SIM/include" -I"$ROOT/include" -I"$ROOT/src" \
    $SRCS \
    -sMODULARIZE=1 -sEXPORT_NAME=MidiumSim -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0 \
    -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=32MB -sSTACK_SIZE=256KB \
    -sFILESYSTEM=0 -sEXPORTED_FUNCTIONS="$EXPORTS" \
    -sEXPORTED_RUNTIME_METHODS='["UTF8ToString","HEAPU8","HEAPF64"]' \
    -o "$OUT/midium_sim.js"

# Inline the module into the page template.
python3 - "$SIM/web/template.html" "$OUT/midium_sim.js" "$SIM/web/index.html" <<'EOF'
import sys
tpl, js, out = sys.argv[1:4]
t = open(tpl, encoding="utf-8").read()
j = open(js, encoding="utf-8").read().replace("</script", "<\\/script")
assert "/*__MIDIUM_SIM_JS__*/" in t
open(out, "w", encoding="utf-8").write(t.replace("/*__MIDIUM_SIM_JS__*/", j, 1))
EOF
ls -l "$OUT/midium_sim.js" "$SIM/web/index.html"
