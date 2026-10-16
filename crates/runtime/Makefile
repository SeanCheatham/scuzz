# Use clang by default. A command-line CC value takes precedence.
# The built-in CC=cc prevents the use of ?= here.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
FFI_SKIA := $(ROOT)/crates/ffi-skia
EMBEDDER := $(ROOT)/crates/embedder-desktop
MOBILE := $(ROOT)/crates/embedder-mobile
UNAME_S := $(shell uname -s)
# HTTPS uses OpenSSL. Homebrew supplies its macOS prefix.
ifeq ($(UNAME_S),Darwin)
SSL_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
ifneq ($(SSL_PREFIX),)
SSL_INCLUDES := -I$(SSL_PREFIX)/include
SSL_LINK := -L$(SSL_PREFIX)/lib -lssl -lcrypto
else
SSL_INCLUDES :=
SSL_LINK := -lssl -lcrypto
endif
else
SSL_INCLUDES :=
SSL_LINK := -lssl -lcrypto
endif
INCLUDES := -Iinclude -I$(FFI_SKIA)/include -I$(EMBEDDER)/include -I$(MOBILE)/include $(SSL_INCLUDES)
SRC := src/runtime.c src/resource.c src/ref.c src/deferred.c src/queue.c src/stream.c \
	src/theme.c src/signal.c src/image.c src/text.c src/text_layout.c src/view.c src/ui.c \
	src/ui_script.c src/lang_ui.c \
	src/list.c src/map.c src/fs.c src/sys.c src/clock.c src/hash.c src/hex.c src/base64.c src/bytes.c src/random.c src/uuid.c src/net.c \
	src/net_http.c src/net_request.c src/net_sock.c src/json.c src/testrt.c src/impurity.c
BUILDDIR ?= build
OBJ := $(SRC:src/%.c=$(BUILDDIR)/%.o) $(BUILDDIR)/image_dec.o

.PHONY: all clean test test-asan test-ubsan lib lib-asan ffi-skia

all: lib

$(BUILDDIR):
	mkdir -p $@

FFI_SKIA_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2

# Use separate paint flags to keep sanitizer builds independent.
# The runtime archive needs paint headers, but no paint archive.
ffi-skia:
	@$(MAKE) --no-print-directory -C $(FFI_SKIA) lib CC=$(CC) CFLAGS='$(FFI_SKIA_CFLAGS)'

$(BUILDDIR)/%.o: src/%.c include/scuzz_rt.h include/scuzz_ui.h src/rt_util.h src/rt_locale.h src/ui_script.h src/net_transport.h src/image.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/text.o $(BUILDDIR)/text_layout.o $(BUILDDIR)/view.o $(BUILDDIR)/test_ui: src/text.h $(FFI_SKIA)/include/sk_text.h

$(BUILDDIR)/image_dec.o: $(FFI_SKIA)/src/image_dec.c $(FFI_SKIA)/src/image_dec.h $(FFI_SKIA)/src/stb_image.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(FFI_SKIA)/src -c $< -o $@

lib: $(BUILDDIR)/libscuzz_rt.a

$(BUILDDIR)/libscuzz_rt.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Native text needs C++. Skia also needs zlib, bzip2, and macOS text frameworks.
# CoreFoundation runs AppKit work on the macOS main thread.
TEXT_LINK := $(if $(filter Darwin,$(UNAME_S)),-lc++,-lstdc++)
SKIA_LINK = $(TEXT_LINK) $$([ -f $(FFI_SKIA)/build/sk_capi_backend ] && grep -qx skia $(FFI_SKIA)/build/sk_capi_backend && \
	if [ "$$(uname -s)" = Darwin ]; then \
	  echo -lm -lz -lbz2 -framework CoreFoundation -framework CoreGraphics -framework CoreText -framework Foundation -framework Carbon; \
	else echo -lm -lz -lbz2; fi)
GPU_LINK = $$([ -f $(FFI_SKIA)/build/sk_capi_backend ] && grep -qx gpu $(FFI_SKIA)/build/sk_capi_backend && \
	if [ "$$(uname -s)" = Darwin ]; then echo -framework OpenGL; else echo -lEGL -lGLESv2; fi)
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

$(BUILDDIR)/reload_a.$(RELOAD_EXT): tests/reload_a.c include/scuzz_ui.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC $(RELOAD_LIB) tests/reload_a.c -o $@

$(BUILDDIR)/reload_b.$(RELOAD_EXT): tests/reload_b.c include/scuzz_ui.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -fPIC $(RELOAD_LIB) tests/reload_b.c -o $@

$(BUILDDIR)/reload_bad.$(RELOAD_EXT): tests/reload_b.c include/scuzz_ui.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -DRELOAD_SCHEMA='"counter:string"' -fPIC $(RELOAD_LIB) tests/reload_b.c -o $@

# Relink tests after paint selection so they use the selected archive.
$(BUILDDIR)/test_io: tests/test_io.c src/rt_util.h $(BUILDDIR)/libscuzz_rt.a ffi-skia | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) tests/test_io.c -L$(BUILDDIR) -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread -lm $(SSL_LINK) $(HOST_LINK) $(SKIA_LINK) $(GPU_LINK) -o $@

$(BUILDDIR)/test_ui: tests/test_ui.c $(BUILDDIR)/libscuzz_rt.a $(BUILDDIR)/reload_a.$(RELOAD_EXT) $(BUILDDIR)/reload_b.$(RELOAD_EXT) $(BUILDDIR)/reload_bad.$(RELOAD_EXT) ffi-skia | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $(EXPORT_LINK) tests/test_ui.c -L$(BUILDDIR) -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread -lm $(SSL_LINK) $(HOST_LINK) $(SKIA_LINK) $(GPU_LINK) -o $@

$(BUILDDIR)/test_native_motion: tests/test_native_motion.c $(BUILDDIR)/libscuzz_rt.a ffi-skia | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) tests/test_native_motion.c -L$(BUILDDIR) -L$(FFI_SKIA)/build \
		-lscuzz_rt -lsk_capi -lpthread -lm $(SSL_LINK) $(HOST_LINK) $(SKIA_LINK) $(GPU_LINK) -o $@

test: $(BUILDDIR)/test_io $(BUILDDIR)/test_ui $(BUILDDIR)/test_native_motion
	"$(abspath $(BUILDDIR))/test_io"
	"$(abspath $(BUILDDIR))/test_ui"
	"$(abspath $(BUILDDIR))/test_native_motion"

# ASan C tests keep leak detection off. Compiled corpus replay uses
# lib-asan with leak detection on. Keep each build separate.
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
lib-asan:
	@asan_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/scuzz-asan.XXXXXX") || exit 1; \
	trap 'rm -rf "$$asan_dir"' 0; \
	if printf '%s\n' 'int main(void){return 0;}' | $(CC) $(ASAN_CFLAGS) -x c - -o "$$asan_dir/probe" 2>/dev/null; then \
		$(MAKE) lib BUILDDIR=build/asan CFLAGS='$(ASAN_CFLAGS)'; \
	else \
		echo "lib-asan: AddressSanitizer not supported by $(CC) on this host" >&2; \
		exit 1; \
	fi

test-asan:
	@asan_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/scuzz-asan.XXXXXX") || exit 1; \
	trap 'rm -rf "$$asan_dir"' 0; \
	if ! printf '%s\n' '#include <stdlib.h>' 'int main(void){volatile char *p=malloc(1);p[1]=0;free((void *)p);return 0;}' | $(CC) $(ASAN_CFLAGS) -x c - -o "$$asan_dir/probe" 2>/dev/null; then \
		echo "test-asan: AddressSanitizer not supported by $(CC) on this host; skipping"; exit 0; \
	fi; \
	if ASAN_OPTIONS=detect_leaks=0:symbolize=0 "$$asan_dir/probe" >"$$asan_dir/output" 2>&1; then \
		echo "test-asan: the known buffer overflow does not fail" >&2; exit 1; \
	fi; \
	if ! grep -q 'AddressSanitizer: heap-buffer-overflow' "$$asan_dir/output"; then \
		cat "$$asan_dir/output" >&2; \
		echo "test-asan: the buffer overflow probe has no ASan evidence" >&2; exit 1; \
	fi; \
	echo "ASan rejects the known buffer overflow."; \
	$(MAKE) BUILDDIR=build/asan-tests/build CFLAGS='$(ASAN_CFLAGS)' build/asan-tests/build/test_io build/asan-tests/build/test_ui build/asan-tests/build/test_native_motion && \
	(cd build/asan-tests && ASAN_OPTIONS=detect_leaks=0 ./build/test_io && ASAN_OPTIONS=detect_leaks=0 ./build/test_ui && ASAN_OPTIONS=detect_leaks=0 ./build/test_native_motion)

# Keep UBSan binaries and file fixtures separate from the normal build.
UBSAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer

test-ubsan:
	@ubsan_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/scuzz-ubsan.XXXXXX") || exit 1; \
	trap 'rm -rf "$$ubsan_dir"' 0; \
	if ! printf '%s\n' '#include <limits.h>' 'int main(void){volatile int n=INT_MAX;volatile int result=n+1;(void)result;return 0;}' | $(CC) $(UBSAN_CFLAGS) -x c - -o "$$ubsan_dir/probe"; then \
		echo "test-ubsan: cannot compile and link UBSan with $(CC)" >&2; exit 1; \
	fi; \
	if "$$ubsan_dir/probe" >"$$ubsan_dir/output" 2>&1; then \
		echo "test-ubsan: the known overflow does not fail" >&2; exit 1; \
	fi; \
	if ! grep -q 'signed integer overflow' "$$ubsan_dir/output"; then \
		cat "$$ubsan_dir/output" >&2; \
		echo "test-ubsan: the overflow probe has no UBSan evidence" >&2; exit 1; \
	fi; \
	echo "UBSan rejects the known signed overflow."; \
	$(MAKE) BUILDDIR=build/ubsan/build CFLAGS='$(UBSAN_CFLAGS)' build/ubsan/build/test_io build/ubsan/build/test_ui build/ubsan/build/test_native_motion && \
	(cd build/ubsan && ./build/test_io && ./build/test_ui && ./build/test_native_motion)

clean:
	rm -rf build
	$(MAKE) -C $(FFI_SKIA) clean
