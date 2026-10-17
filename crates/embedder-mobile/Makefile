# Plain = so a command-line CC=gcc still wins. ?= would lose to make's built-in CC=cc default.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
RUNTIME := $(ROOT)/crates/runtime
INCLUDES := -Iinclude -I$(RUNTIME)/include
SRC := src/host_shell.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean lib test

all: lib

build:
	mkdir -p build

build/%.o: src/%.c include/scuzz_mobile.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_mobile.a

build/libscuzz_mobile.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_host: tests/test_host.c build/libscuzz_mobile.a | build
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $< -Lbuild -lscuzz_mobile -o $@

test: build/test_host
	./build/test_host

clean:
	rm -rf build
