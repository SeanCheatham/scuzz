CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
TRIPLE ?= $(shell $(ROOT)/scripts/skia_triple.sh)
PREBUILT_DIR := $(ROOT)/third_party/skia/prebuilt/$(TRIPLE)
PREBUILT_LIB := $(PREBUILT_DIR)/libsk_capi.a
INCLUDES := -Iinclude -Isrc
SRC := src/sk_sw.c src/png_enc.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib lib-skia lib-sk-sw lib-gpu ensure-prebuilt

all: lib

build:
	mkdir -p build

# Default: pinned Skia prebuilt (fetch if needed). Opt out with SCUZZ_SKIA=sk_sw.
# SCUZZ_SKIA=gpu: in-tree sk_sw paint + OpenGL present/readback.
lib: ensure-prebuilt
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ]; then \
	  $(MAKE) lib-sk-sw; \
	elif [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  $(MAKE) lib-gpu; \
	elif [ -f "$(PREBUILT_LIB)" ]; then \
	  $(MAKE) lib-skia; \
	else \
	  echo "ffi-skia: missing $(PREBUILT_LIB) (set SCUZZ_SKIA=sk_sw to use in-tree sk_sw)" >&2; \
	  exit 1; \
	fi

ensure-prebuilt:
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ] || [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  echo "ffi-skia: SCUZZ_SKIA=$${SCUZZ_SKIA} — in-tree software paint"; \
	else \
	  "$(ROOT)/scripts/fetch_skia.sh"; \
	fi

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
GPU_LIBS := -framework OpenGL
else
GPU_LIBS := -lEGL -lGLESv2
endif

build/%.o: src/%.c include/sk_capi.h src/png_enc.h src/sk_gpu.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/sk_gpu.o: src/sk_gpu.c src/sk_gpu.h | build
	$(CC) $(CFLAGS) -Wno-deprecated-declarations $(INCLUDES) -c $< -o $@

lib-sk-sw: $(OBJ) build/sk_gpu_none.o | build
	rm -f build/libsk_capi.a
	ar rcs build/libsk_capi.a $(OBJ) build/sk_gpu_none.o
	echo sk_sw > build/sk_capi_backend

lib-gpu: $(OBJ) build/sk_gpu.o | build
	rm -f build/libsk_capi.a
	ar rcs build/libsk_capi.a $(OBJ) build/sk_gpu.o
	echo gpu > build/sk_capi_backend

lib-skia: build/sk_gpu_skia_stub.o | build
	@test -f "$(PREBUILT_LIB)"
	rm -f build/*.a
	cp -f "$(PREBUILT_LIB)" build/libsk_capi.a
	ar rcs build/libsk_capi.a build/sk_gpu_skia_stub.o
	echo skia > build/sk_capi_backend

build/test_skia: tests/test_skia.c lib
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  if [ "$$(uname -s)" = Darwin ]; then \
	    extra="-lc++ -lm -lz -lbz2 -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework Foundation -framework Carbon"; \
	  else \
	    extra="-lstdc++ -lm -lz -lbz2"; \
	  fi; \
	elif [ -f build/sk_capi_backend ] && grep -qx gpu build/sk_capi_backend; then \
	  extra="$(GPU_LIBS)"; \
	fi; \
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_skia.c -Lbuild -lsk_capi $$extra -lpthread -o $@

# Canvas clip against in-tree sk_sw. The pinned Skia archive does not export save/clip.
build/test_sk_clip: tests/test_sk_clip.c src/sk_sw.c src/png_enc.c src/sk_gpu_none.c include/sk_capi.h src/png_enc.h | build
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sk_clip.c src/sk_sw.c src/png_enc.c src/sk_gpu_none.c -o $@

build/test_sk_gpu: tests/test_sk_gpu.c lib
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sk_gpu.c -Lbuild -lsk_capi $(GPU_LIBS) -lpthread -o $@

test: build/test_skia build/test_sk_clip
	./build/test_skia
	./build/test_sk_clip
	@if [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  $(MAKE) build/test_sk_gpu; \
	  ./build/test_sk_gpu; \
	fi

clean:
	rm -rf build
