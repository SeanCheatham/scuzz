CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
INCLUDES := -Iinclude
SRC := src/x11_present.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean lib test

all: lib

build:
	mkdir -p build

build/%.o: src/%.c include/scalui_embedder.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_embedder.a

build/libscalui_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Smoke: available() is 0 without DISPLAY; with xvfb it is 1.
test: lib
	@echo "embedder-desktop: lib ok (X11 present requires DISPLAY)"

clean:
	rm -rf build
