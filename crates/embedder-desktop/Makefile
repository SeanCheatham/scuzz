# Linux X11 Window presenter. Skipped on other hosts (Headless/Mobile still work).
UNAME_S := $(shell uname -s)
CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
INCLUDES := -Iinclude
SRC := src/x11_present.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean lib test

all: lib

ifeq ($(UNAME_S),Linux)

build:
	mkdir -p build

build/%.o: src/%.c include/scalui_embedder.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_embedder.a

build/libscalui_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

test: lib
	@echo "embedder-desktop: lib ok (X11 present requires DISPLAY)"

else

lib:
	@echo "embedder-desktop: skip (Linux X11 only; host=$(UNAME_S))"

test:
	@echo "embedder-desktop: skip (Linux X11 only)"

endif

clean:
	rm -rf build
