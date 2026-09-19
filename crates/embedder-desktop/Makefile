# Desktop presenter: Linux X11 or Darwin Cocoa. Skipped elsewhere (Headless still works).
UNAME_S := $(shell uname -s)
# make has a built-in CC=cc default, so ?= never fires. Plain = keeps
# clang the default while `make CC=gcc` on the command line still wins.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
OBJCFLAGS ?= -Wall -Wextra -Werror -O2 -fobjc-arc
ROOT := $(abspath ../..)
RUNTIME := $(ROOT)/crates/runtime
INCLUDES := -Iinclude -I$(RUNTIME)/include

.PHONY: all clean lib test

all: lib

build:
	mkdir -p build

ifeq ($(UNAME_S),Linux)

SRC := src/x11_present.c
OBJ := build/x11_present.o

build/%.o: src/%.c include/scuzz_embedder.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_embedder.a

build/libscuzz_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_headless: tests/test_headless.c build/libscuzz_embedder.a | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_headless.c -Lbuild -lscuzz_embedder -lX11 -o $@

build/test_idle_events: tests/test_idle_events.c build/libscuzz_embedder.a | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_idle_events.c -Lbuild -lscuzz_embedder -lX11 -o $@

test: lib build/test_headless build/test_idle_events
	env -u DISPLAY ./build/test_headless
	@if [ -n "$$DISPLAY" ]; then ./build/test_idle_events; else echo "embedder-desktop: skip idle events (no DISPLAY)"; fi

else ifeq ($(UNAME_S),Darwin)

SRC := src/macos_present.m $(RUNTIME)/src/net_apple.m
OBJ := build/macos_present.o build/net_apple.o

build/net_apple.o: $(RUNTIME)/src/net_apple.m $(RUNTIME)/src/net_transport.h $(RUNTIME)/src/rt_util.h $(RUNTIME)/include/scuzz_rt.h | build
	$(CC) $(OBJCFLAGS) $(INCLUDES) -I$(RUNTIME)/src -c $< -o $@

build/%.o: src/%.m include/scuzz_embedder.h | build
	$(CC) $(OBJCFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_embedder.a

build/libscuzz_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

test: lib
	@echo "embedder-desktop: lib ok (Cocoa present)"

else

lib:
	@echo "embedder-desktop: skip (Linux/Darwin only; host=$(UNAME_S))"

test:
	@echo "embedder-desktop: skip (Linux/Darwin only)"

endif

clean:
	rm -rf build
