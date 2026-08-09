CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
FFI_SKIA := $(ROOT)/crates/ffi-skia
EMBEDDER := $(ROOT)/crates/embedder-desktop
MOBILE := $(ROOT)/crates/embedder-mobile
INCLUDES := -Iinclude -I$(FFI_SKIA)/include -I$(EMBEDDER)/include -I$(MOBILE)/include
SRC := src/runtime.c src/resource.c src/ref.c src/deferred.c src/queue.c \
	src/effects.c src/theme.c src/signal.c src/view.c src/ui.c src/demos.c \
	src/list.c src/fs.c src/sys.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib ffi-skia

all: lib

build:
	mkdir -p build

ffi-skia:
	$(MAKE) -C $(FFI_SKIA) lib CC=$(CC)

build/%.o: src/%.c include/scalui_rt.h include/scalui_ui.h | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscalui_rt.a

build/libscalui_rt.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_io: tests/test_io.c build/libscalui_rt.a | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_io.c -Lbuild -L$(FFI_SKIA)/build \
		-lscalui_rt -lsk_capi -o $@

build/test_ui: tests/test_ui.c build/libscalui_rt.a | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_ui.c -Lbuild -L$(FFI_SKIA)/build \
		-lscalui_rt -lsk_capi -o $@

test: build/test_io build/test_ui
	./build/test_io
	./build/test_ui

clean:
	rm -rf build
	$(MAKE) -C $(FFI_SKIA) clean
