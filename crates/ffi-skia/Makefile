CC ?= clang
CXX ?= clang++
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
TRIPLE ?= $(shell $(ROOT)/scripts/skia_triple.sh)
PREBUILT_DIR := $(ROOT)/third_party/skia/prebuilt/$(TRIPLE)
PREBUILT_LIB := $(PREBUILT_DIR)/libsk_capi.a
PIN := $(ROOT)/third_party/skia/PIN
SKIA_BRANCH := $(shell awk -F= '/^skia_branch=/{print substr($$0,13); exit}' "$(PIN)")
SKIA_HDR_URL := https://skia.googlesource.com/skia/+archive/$(SKIA_BRANCH)/include.tar.gz
INCLUDES := -Iinclude -Isrc
SRC := src/sk_sw.c src/png_enc.c src/sk_mono.c
OBJ := $(SRC:src/%.c=build/%.o)
SHIM_OBJS := build/sk_capi_skia.o build/sk_capi_skia_bridge.o

.PHONY: all clean test lib lib-skia lib-sk-sw lib-gpu ensure-prebuilt

all: lib

build:
	mkdir -p build

# Default: pinned Skia prebuilt (fetch if needed). Opt out with SCUZZ_SKIA=sk_sw.
# SCUZZ_SKIA=gpu: in-tree sk_sw paint + OpenGL present/readback.
lib: ensure-prebuilt
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ]; then \
	  $(MAKE) --no-print-directory lib-sk-sw; \
	elif [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  $(MAKE) --no-print-directory lib-gpu; \
	elif [ -f "$(PREBUILT_LIB)" ]; then \
	  $(MAKE) --no-print-directory lib-skia; \
	else \
	  echo "ffi-skia: missing $(PREBUILT_LIB) (set SCUZZ_SKIA=sk_sw to use in-tree sk_sw)" >&2; \
	  exit 1; \
	fi

ensure-prebuilt:
	@if [ "$${SCUZZ_SKIA:-}" = "sk_sw" ] || [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  :; \
	elif [ ! -f "$(PREBUILT_LIB)" ]; then \
	  "$(ROOT)/scripts/fetch_skia.sh"; \
	fi

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
GPU_LIBS := -framework OpenGL
SKIA_LIBS := -lc++ -lm -lz -lbz2 -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework Foundation -framework Carbon
else
GPU_LIBS := -lEGL -lGLESv2
SKIA_LIBS := -lstdc++ -lm -lz -lbz2
endif

build/%.o: src/%.c include/sk_capi.h src/png_enc.h src/sk_gpu.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/sk_gpu.o: src/sk_gpu.c src/sk_gpu.h | build
	$(CC) $(CFLAGS) -Wno-deprecated-declarations $(INCLUDES) -c $< -o $@

# Headers that match the pin's Skia branch. Not a vendored tree.
build/skia-src/.stamp: | build
	@mkdir -p build/skia-src/include
	@echo "ffi-skia: fetch $(SKIA_BRANCH) include for shim relink"
	@curl -fsSL "$(SKIA_HDR_URL)" -o build/skia-include.tar.gz
	@rm -rf build/skia-src/include
	@mkdir -p build/skia-src/include
	@tar -xzf build/skia-include.tar.gz -C build/skia-src/include
	@test -f build/skia-src/include/core/SkCanvas.h
	@echo "$(SKIA_BRANCH)" > $@

build/sk_capi_skia.o: src/sk_capi_skia.cpp include/sk_capi.h build/skia-src/.stamp
	$(CXX) -std=c++17 -O2 -fPIC -c src/sk_capi_skia.cpp -o $@ \
	  -Ibuild/skia-src -DSK_RELEASE -DSCUZZ_SKIA_EMBEDDED_FONT

build/sk_capi_skia_bridge.o: src/sk_capi_skia_bridge.c include/sk_capi.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib-sk-sw: $(OBJ) build/sk_gpu_none.o | build
	@if [ -f build/libsk_capi.a ] && [ -f build/sk_capi_backend ] && grep -qx sk_sw build/sk_capi_backend; then \
	  fresh=1; \
	  for o in $(OBJ) build/sk_gpu_none.o; do \
	    if [ $$o -nt build/libsk_capi.a ]; then fresh=0; fi; \
	  done; \
	  if [ $$fresh -eq 1 ]; then exit 0; fi; \
	fi; \
	rm -f build/libsk_capi.a; \
	ar rcs build/libsk_capi.a $(OBJ) build/sk_gpu_none.o; \
	echo sk_sw > build/sk_capi_backend

lib-gpu: $(OBJ) build/sk_gpu.o | build
	@if [ -f build/libsk_capi.a ] && [ -f build/sk_capi_backend ] && grep -qx gpu build/sk_capi_backend; then \
	  fresh=1; \
	  for o in $(OBJ) build/sk_gpu.o; do \
	    if [ $$o -nt build/libsk_capi.a ]; then fresh=0; fi; \
	  done; \
	  if [ $$fresh -eq 1 ]; then exit 0; fi; \
	fi; \
	rm -f build/libsk_capi.a; \
	ar rcs build/libsk_capi.a $(OBJ) build/sk_gpu.o; \
	echo gpu > build/sk_capi_backend

# Pin supplies Skia objects + font. Replace the shim so the C ABI matches
# sk_capi.h (save/clip/restore, RGBA peek). Fail if clip symbols are missing.
lib-skia: build/sk_gpu_skia_stub.o build/sk_mono.o $(SHIM_OBJS) | build
	@test -f "$(PREBUILT_LIB)"
	@if [ -f build/libsk_capi.a ] && [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend \
	    && [ ! "$(PREBUILT_LIB)" -nt build/libsk_capi.a ] \
	    && [ ! build/sk_gpu_skia_stub.o -nt build/libsk_capi.a ] \
	    && [ ! build/sk_mono.o -nt build/libsk_capi.a ] \
	    && [ ! build/sk_capi_skia.o -nt build/libsk_capi.a ] \
	    && [ ! build/sk_capi_skia_bridge.o -nt build/libsk_capi.a ]; then \
	  if nm build/libsk_capi.a | grep -E ' T _?sk_canvas_clip_rect' >/dev/null && \
	     nm build/libsk_capi.a | grep -E ' T _?sk_canvas_save' >/dev/null && \
	     nm build/libsk_capi.a | grep -E ' T _?sk_canvas_restore' >/dev/null; then \
	    exit 0; \
	  fi; \
	fi; \
	rm -f build/*.a; \
	cp -f "$(PREBUILT_LIB)" build/libsk_capi.a; \
	ar d build/libsk_capi.a sk_capi_skia.o sk_capi_skia_bridge.o; \
	ar rcs build/libsk_capi.a $(SHIM_OBJS) build/sk_gpu_skia_stub.o build/sk_mono.o; \
	if ! nm build/libsk_capi.a | grep -E ' T _?sk_canvas_clip_rect' >/dev/null; then \
	  echo "ffi-skia: linked archive lacks sk_canvas_clip_rect" >&2; \
	  exit 1; \
	fi; \
	if ! nm build/libsk_capi.a | grep -E ' T _?sk_canvas_save' >/dev/null; then \
	  echo "ffi-skia: linked archive lacks sk_canvas_save" >&2; \
	  exit 1; \
	fi; \
	if ! nm build/libsk_capi.a | grep -E ' T _?sk_canvas_restore' >/dev/null; then \
	  echo "ffi-skia: linked archive lacks sk_canvas_restore" >&2; \
	  exit 1; \
	fi; \
	echo skia > build/sk_capi_backend

build/test_skia: tests/test_skia.c lib
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  extra="$(SKIA_LIBS)"; \
	elif [ -f build/sk_capi_backend ] && grep -qx gpu build/sk_capi_backend; then \
	  extra="$(GPU_LIBS)"; \
	fi; \
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_skia.c -Lbuild -lsk_capi $$extra -lpthread -o $@

# Clip / RGBA / UTF-8 against the archive `lib` actually built.
build/test_sk_clip: tests/test_sk_clip.c lib
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  extra="$(SKIA_LIBS)"; \
	elif [ -f build/sk_capi_backend ] && grep -qx gpu build/sk_capi_backend; then \
	  extra="$(GPU_LIBS)"; \
	fi; \
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_sk_clip.c -Lbuild -lsk_capi $$extra -lpthread -o $@

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
