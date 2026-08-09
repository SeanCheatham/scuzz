CC ?= clang
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

build/%.o: src/%.c include/scalui_mobile.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_mobile.a

build/libscalui_mobile.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Host shell is gated by SCALUI_MOBILE_SHELL; available() is 0 without it.
test: lib
	@echo "embedder-mobile: lib ok (host shell requires SCALUI_MOBILE_SHELL=1)"

clean:
	rm -rf build
