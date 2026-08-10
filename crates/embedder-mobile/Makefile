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

build/%.o: src/%.c include/scuzz_mobile.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_mobile.a

build/libscuzz_mobile.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Host shell is gated by SCUZZ_MOBILE_SHELL; available() is 0 without it.
test: lib
	@echo "embedder-mobile: lib ok (host shell requires SCUZZ_MOBILE_SHELL=1)"

clean:
	rm -rf build
