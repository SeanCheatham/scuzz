CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
FFI_SKIA := $(ROOT)/crates/ffi-skia
EMBEDDER := $(ROOT)/crates/embedder-desktop
MOBILE := $(ROOT)/crates/embedder-mobile
INCLUDES := -Iinclude -I$(FFI_SKIA)/include -I$(EMBEDDER)/include -I$(MOBILE)/include
SRC := src/runtime.c src/resource.c src/ref.c src/deferred.c src/queue.c \
	src/effects.c src/theme.c src/signal.c src/view.c src/ui.c \
	src/lang_ui.c \
	src/list.c src/fs.c src/sys.c src/clock.c src/random.c src/net.c \
	src/testrt.c src/impurity.c src/anim.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test test-asan lib ffi-skia

all: lib

build:
	mkdir -p build

ffi-skia:
	$(MAKE) -C $(FFI_SKIA) lib CC=$(CC)

build/%.o: src/%.c include/scuzz_rt.h include/scuzz_ui.h | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_rt.a

build/libscuzz_rt.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/test_io: tests/test_io.c build/libscuzz_rt.a | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_io.c -Lbuild -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread -o $@

build/test_ui: tests/test_ui.c build/libscuzz_rt.a | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_ui.c -Lbuild -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread -o $@

test: build/test_io build/test_ui
	./build/test_io
	./build/test_ui

# Optional ASan/LSan; not part of default `make test` (slow / host-dependent).
# Best-effort: skips with a message if the toolchain cannot link -fsanitize=address.
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
test-asan:
	@if echo 'int main(void){return 0;}' | $(CC) $(ASAN_CFLAGS) -x c - -o /tmp/scuzz_asan_probe 2>/dev/null; then \
		rm -f /tmp/scuzz_asan_probe; \
		$(MAKE) clean && $(MAKE) test CFLAGS='$(ASAN_CFLAGS)'; \
	else \
		echo "test-asan: AddressSanitizer not supported by $(CC) on this host; skipping"; \
	fi

clean:
	rm -rf build
	$(MAKE) -C $(FFI_SKIA) clean
