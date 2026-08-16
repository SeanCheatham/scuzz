CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
TRIPLE ?= $(shell $(ROOT)/scripts/skia_triple.sh)
PREBUILT_DIR := $(ROOT)/third_party/skia/prebuilt/$(TRIPLE)
PREBUILT_LIB := $(PREBUILT_DIR)/libsk_capi.a
INCLUDES := -Iinclude -Isrc
SRC := src/sk_sw.c src/png_enc.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib lib-skia lib-sk-sw ensure-prebuilt

all: lib

build:
	mkdir -p build

# Default: pinned Skia prebuilt (fetch if needed). Opt out with SCUZZ_SKIA=sk_sw.
lib: ensure-prebuilt
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ]; then \
	  $(MAKE) lib-sk-sw; \
	elif [ -f "$(PREBUILT_LIB)" ]; then \
	  $(MAKE) lib-skia; \
	else \
	  echo "ffi-skia: missing $(PREBUILT_LIB) (set SCUZZ_SKIA=sk_sw to use in-tree sk_sw)" >&2; \
	  exit 1; \
	fi

ensure-prebuilt:
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ]; then \
	  echo "ffi-skia: SCUZZ_SKIA=sk_sw — in-tree software backend"; \
	else \
	  "$(ROOT)/scripts/fetch_skia.sh"; \
	fi

build/%.o: src/%.c include/sk_capi.h src/png_enc.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib-sk-sw: $(OBJ) | build
	rm -f build/libsk_capi.a
	ar rcs build/libsk_capi.a $(OBJ)
	echo sk_sw > build/sk_capi_backend

lib-skia: | build
	@test -f "$(PREBUILT_LIB)"
	rm -f build/*.a
	cp -f "$(PREBUILT_LIB)" build/libsk_capi.a
	echo skia > build/sk_capi_backend

build/test_skia: tests/test_skia.c lib
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  if [ "$$(uname -s)" = Darwin ]; then \
	    extra="-lc++ -lm -lz -lbz2 -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework Foundation -framework Carbon"; \
	  else \
	    extra="-lstdc++ -lm -lz -lbz2"; \
	  fi; \
	fi; \
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_skia.c -Lbuild -lsk_capi $$extra -lpthread -o $@

# Canvas clip against in-tree sk_sw. The pinned Skia archive does not export save/clip.
build/test_sk_clip: tests/test_sk_clip.c src/sk_sw.c src/png_enc.c include/sk_capi.h src/png_enc.h | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sk_clip.c src/sk_sw.c src/png_enc.c -o $@

test: build/test_skia build/test_sk_clip
	./build/test_skia
	./build/test_sk_clip

clean:
	rm -rf build
