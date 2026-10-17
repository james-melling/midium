cmake_minimum_required(VERSION 3.13)

# Default board: Raspberry Pi Pico 2 (RP2350, Cortex-M33)
if (NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
endif()

# Pull in the Pico SDK (PICO_SDK_PATH env var or -DPICO_SDK_PATH=...)
include(pico_sdk_import.cmake)

project(midium VERSION 0.1.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Build-time options
set(MIDIUM_DISPLAY "0" CACHE STRING "0 = auto-detect, 1 = force OLED, 2 = force TFT")
option(MIDIUM_OLED_SSD1306 "OLED is an SSD1306 (Adafruit 938, default) - OFF for SH1106" ON)

set(MIDIUM_SOURCES
    src/main.c
    src/midi_engine.c
    src/midi_io.c
    src/midi_parser.c
    src/midi_decode.c
    src/monitor.c
    src/generator.c
    src/console.c
    src/disp_oled.c
    src/disp_tft.c
    src/joystick.c
    src/ui.c
    src/settings.c
    src/debug_log.c
)

add_executable(midium ${MIDIUM_SOURCES})

target_include_directories(midium PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/src)

target_compile_definitions(midium PRIVATE
    MIDIUM_TARGET=1
    MIDIUM_VERSION="${PROJECT_VERSION}"
    MIDIUM_DISPLAY=${MIDIUM_DISPLAY}
    MIDIUM_OLED_SSD1306=$<BOOL:${MIDIUM_OLED_SSD1306}>
    # Don't stall core0 if the USB host stops reading the debug log
    PICO_STDIO_USB_STDOUT_TIMEOUT_US=2000
)

# Strict warnings on our own sources only (SDK sources keep SDK defaults)
set_source_files_properties(${MIDIUM_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wdouble-promotion;-Wformat=2")

target_link_libraries(midium
    pico_stdlib
    pico_multicore
    hardware_uart
    hardware_i2c
    hardware_spi
    hardware_pwm
    hardware_watchdog
    hardware_sync
)

# Debug log over USB CDC (milestone 3 replaces this with composite MIDI + MSC)
pico_enable_stdio_usb(midium 1)
pico_enable_stdio_uart(midium 0)   # UART0 is MIDI: stdio must never touch it

pico_set_program_name(midium "MIDIum")
pico_set_program_description(midium "NSK MT-1 inline MIDI test / thru box")
pico_set_program_version(midium "${PROJECT_VERSION}")

pico_add_extra_outputs(midium)
