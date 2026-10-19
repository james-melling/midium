# Host-side unit tests for the MIDIum parser, decoders (incl. MSC) and thru/merge engine.
#   make -C test        build and run
#   make -C test clean
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined
INC      = -I../include
SRCS     = test_main.c test_msc.c test_m2.c test_power.c ../src/midi_engine.c ../src/midi_parser.c ../src/midi_decode.c \
           ../src/settings.c \
           ../src/msc_decode.c ../src/timeutil.c ../src/mtc.c ../src/msg_filter.c ../src/valedit.c \
           ../src/bigdigits.c ../src/log_store.c ../src/log_flash_ram.c ../src/power_fsm.c

all: run

test_midium: $(SRCS) $(wildcard ../include/*.h) test_common.h Makefile
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRCS)

run: test_midium
	./test_midium

clean:
	rm -f test_midium

.PHONY: all run clean
