CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
TRIPLE ?= $(shell $(ROOT)/scripts/skia_triple.sh)
PREBUILT_DIR := $(ROOT)/third_party/skia/prebuilt/$(TRIPLE)
PREBUILT_LIB := $(PREBUILT_DIR)/libsk_capi.a
INCLUDES := -Iinclude -Isrc
SRC := src/sk_sw.c src/png_enc.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test lib

all: lib

build:
	mkdir -p build

# Prefer a fetched/real Skia prebuilt when present; otherwise build sk_sw.
ifeq ($(wildcard $(PREBUILT_LIB)),)

build/%.o: src/%.c include/sk_capi.h src/png_enc.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: build/libsk_capi.a build/sk_capi_backend

build/libsk_capi.a: $(OBJ)
	ar rcs $@ $(OBJ)

build/sk_capi_backend: | build
	echo sk_sw > $@

else

lib: build/libsk_capi.a build/sk_capi_backend

build/libsk_capi.a: $(PREBUILT_LIB) | build
	cp -f $(PREBUILT_LIB) $@
	# Optional companion archives from the same prebuilt dir (not named libsk_capi.a).
	@for f in $(PREBUILT_DIR)/*.a; do \
	  b=$$(basename "$$f"); \
	  if [ "$$b" != "libsk_capi.a" ]; then cp -f "$$f" "build/$$b"; fi; \
	done

build/sk_capi_backend: $(PREBUILT_LIB) | build
	echo skia > $@

endif

build/test_skia: tests/test_skia.c build/libsk_capi.a | build
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  extra="-lz -lbz2 -lbrotlidec -lbrotlicommon"; \
	fi; \
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_skia.c -Lbuild -lsk_capi -lstdc++ -lm -lpthread $$extra -o $@

test: build/test_skia
	./build/test_skia

clean:
	rm -rf build
