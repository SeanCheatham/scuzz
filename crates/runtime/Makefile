CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
FFI_SKIA := $(ROOT)/crates/ffi-skia
EMBEDDER := $(ROOT)/crates/embedder-desktop
MOBILE := $(ROOT)/crates/embedder-mobile
INCLUDES := -Iinclude -I$(FFI_SKIA)/include -I$(EMBEDDER)/include -I$(MOBILE)/include
SRC := src/runtime.c src/resource.c src/ref.c src/deferred.c src/queue.c src/stream.c \
	src/theme.c src/signal.c src/view.c src/ui.c \
	src/ui_script.c src/lang_ui.c \
	src/list.c src/map.c src/fs.c src/sys.c src/clock.c src/random.c src/net.c \
	src/testrt.c src/impurity.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test test-asan lib ffi-skia

all: lib

build:
	mkdir -p build

FFI_SKIA_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

ffi-skia:
	# Keep ffi-skia on its own CFLAGS so optional ASan runs do not leave
	# instrumented objects for later non-ASan `make -C ffi-skia test`.
	$(MAKE) -C $(FFI_SKIA) lib CC=$(CC) CFLAGS='$(FFI_SKIA_CFLAGS)'

build/%.o: src/%.c include/scuzz_rt.h include/scuzz_ui.h src/rt_util.h src/ui_script.h | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libscuzz_rt.a

build/libscuzz_rt.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Skia CPU prebuilt: C++ + zlib/bz2 (+ Darwin text frameworks).
# CoreFoundation: macOS runtime parks main in CFRunLoop for AppKit hops.
SKIA_LINK = $$([ -f $(FFI_SKIA)/build/sk_capi_backend ] && grep -qx skia $(FFI_SKIA)/build/sk_capi_backend && \
	if [ "$$(uname -s)" = Darwin ]; then \
	  echo -lc++ -lm -lz -lbz2 -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework Foundation -framework Carbon; \
	else echo -lstdc++ -lm -lz -lbz2; fi)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
HOST_LINK := -framework CoreFoundation
EXPORT_LINK :=
RELOAD_EXT := dylib
RELOAD_LIB := -dynamiclib -undefined dynamic_lookup
else
HOST_LINK := -ldl
EXPORT_LINK := -rdynamic
RELOAD_EXT := so
RELOAD_LIB := -shared
endif

build/reload_a.$(RELOAD_EXT): tests/reload_a.c include/scuzz_ui.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC $(RELOAD_LIB) tests/reload_a.c -o $@

build/reload_b.$(RELOAD_EXT): tests/reload_b.c include/scuzz_ui.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC $(RELOAD_LIB) tests/reload_b.c -o $@

build/test_io: tests/test_io.c build/libscuzz_rt.a | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_io.c -Lbuild -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread $(HOST_LINK) $(SKIA_LINK) -o $@

build/test_ui: tests/test_ui.c build/libscuzz_rt.a build/reload_a.$(RELOAD_EXT) build/reload_b.$(RELOAD_EXT) | build ffi-skia
	$(CC) $(CFLAGS) $(INCLUDES) $(EXPORT_LINK) tests/test_ui.c -Lbuild -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread $(HOST_LINK) $(SKIA_LINK) -o $@

test: build/test_io build/test_ui
	./build/test_io
	./build/test_ui

# Optional ASan. Not part of default `make test` (slow / host-dependent).
# Skip if -fsanitize=address cannot link. Exit-time leaks are
# known (see docs/gaps.md Memory residual). LSan stays off so ASan still
# catches UAF/OOB without failing the optional CI step on leftover allocs.
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
test-asan:
	@if echo 'int main(void){return 0;}' | $(CC) $(ASAN_CFLAGS) -x c - -o /tmp/scuzz_asan_probe 2>/dev/null; then \
		rm -f /tmp/scuzz_asan_probe; \
		$(MAKE) clean && \
		ASAN_OPTIONS=detect_leaks=0 $(MAKE) test CFLAGS='$(ASAN_CFLAGS)' && \
		$(MAKE) clean; \
	else \
		echo "test-asan: AddressSanitizer not supported by $(CC) on this host; skipping"; \
	fi

clean:
	rm -rf build
	$(MAKE) -C $(FFI_SKIA) clean
