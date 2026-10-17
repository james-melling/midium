# Host-side unit tests for the MIDIum parser, decoder and thru/merge engine.
#   make -C test        build and run
#   make -C test clean
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Werror -fsanitize=address,undefined
INC      = -I../include
SRCS     = test_main.c ../src/midi_engine.c ../src/midi_parser.c ../src/midi_decode.c

all: run

test_midium: $(SRCS) $(wildcard ../include/*.h)
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRCS)

run: test_midium
	./test_midium

clean:
	rm -f test_midium

.PHONY: all run clean
