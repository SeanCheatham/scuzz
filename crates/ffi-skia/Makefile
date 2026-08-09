CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
INCLUDES := -Iinclude -Isrc
SRC := src/sk_sw.c src/png_enc.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib

all: lib

build:
	mkdir -p build

build/%.o: src/%.c include/sk_capi.h src/png_enc.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libsk_capi.a

build/libsk_capi.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_skia: tests/test_skia.c build/libsk_capi.a | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_skia.c -Lbuild -lsk_capi -o $@

test: build/test_skia
	./build/test_skia

clean:
	rm -rf build
