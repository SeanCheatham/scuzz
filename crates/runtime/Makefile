CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
INCLUDES := -Iinclude
SRC := src/runtime.c src/resource.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib

all: lib

build:
	mkdir -p build

build/%.o: src/%.c include/scalui_rt.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_rt.a

build/libscalui_rt.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_io: tests/test_io.c build/libscalui_rt.a | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_io.c -Lbuild -lscalui_rt -o $@

test: build/test_io
	./build/test_io

clean:
	rm -rf build
