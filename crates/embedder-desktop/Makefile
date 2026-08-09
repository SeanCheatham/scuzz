# Window presenter: Linux X11 or Darwin Cocoa. Skipped elsewhere (Headless still works).
UNAME_S := $(shell uname -s)
CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
OBJCFLAGS ?= -Wall -Wextra -Werror -O2 -fobjc-arc
INCLUDES := -Iinclude

.PHONY: all clean lib test

all: lib

build:
	mkdir -p build

ifeq ($(UNAME_S),Linux)

SRC := src/x11_present.c
OBJ := build/x11_present.o

build/%.o: src/%.c include/scalui_embedder.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_embedder.a

build/libscalui_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

test: lib
	@echo "embedder-desktop: lib ok (X11 present requires DISPLAY)"

else ifeq ($(UNAME_S),Darwin)

SRC := src/macos_present.m
OBJ := build/macos_present.o

build/%.o: src/%.m include/scalui_embedder.h | build
	$(CC) $(OBJCFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_embedder.a

build/libscalui_embedder.a: $(OBJ)
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
