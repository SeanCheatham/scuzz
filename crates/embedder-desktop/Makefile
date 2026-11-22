# Desktop presenter: Linux X11 or Darwin Cocoa. Skipped elsewhere (Headless still works).
UNAME_S := $(shell uname -s)
# Plain = so a command-line CC=gcc still wins. ?= would lose to make's built-in CC=cc default.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
OBJCFLAGS ?= -Wall -Wextra -Werror -O2 -fobjc-arc
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
ASAN_OBJCFLAGS := -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer -fobjc-arc
UBSAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
BUILDDIR ?= build
ROOT := $(abspath ../..)
RUNTIME := $(ROOT)/crates/runtime
FFI_SKIA := $(ROOT)/crates/ffi-skia
INCLUDES := -Iinclude -I$(RUNTIME)/include

.PHONY: all clean lib lib-asan lib-ubsan test test-asan test-ubsan

all: lib

$(BUILDDIR):
	mkdir -p $@

ifeq ($(UNAME_S),Linux)

INCLUDES += $(shell pkg-config --cflags dbus-1 2>/dev/null)

.PHONY: dbus-check
dbus-check:
	@command -v pkg-config >/dev/null 2>&1 && pkg-config --exists dbus-1 || { echo "Install: sudo apt-get install pkg-config libdbus-1-dev" >&2; exit 1; }

$(BUILDDIR)/x11_present.o $(BUILDDIR)/test_motion $(BUILDDIR)/test_accessibility: | dbus-check

SRC := src/x11_present.c
OBJ := $(BUILDDIR)/x11_present.o
DBUS_LIBS = $(shell pkg-config --libs dbus-1)
SKIA_LINK = -lstdc++ $$([ -f $(FFI_SKIA)/build/sk_capi_backend ] && grep -qx skia $(FFI_SKIA)/build/sk_capi_backend && echo -lm -lz -lbz2 || echo -lm)
GPU_LINK = $$([ -f $(FFI_SKIA)/build/sk_capi_backend ] && grep -qx gpu $(FFI_SKIA)/build/sk_capi_backend && echo -lEGL -lGLESv2)

$(BUILDDIR)/%.o: src/%.c include/scuzz_embedder.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: $(BUILDDIR)/libscuzz_embedder.a

lib-asan:
	+@$(MAKE) --no-print-directory lib BUILDDIR=build/asan CFLAGS='$(ASAN_CFLAGS)'

lib-ubsan:
	+@$(MAKE) --no-print-directory lib BUILDDIR=build/ubsan CFLAGS='$(UBSAN_CFLAGS)'

$(BUILDDIR)/libscuzz_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Keep assertions active when the library uses release flags.
$(BUILDDIR)/test_idle_events $(BUILDDIR)/test_resize: tests/x11_test.h

$(BUILDDIR)/test_accessibility: tests/test_accessibility.c $(BUILDDIR)/libscuzz_embedder.a | $(BUILDDIR)
	+@$(MAKE) --no-print-directory -C $(RUNTIME) lib ffi-skia CC=$(CC)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) tests/test_accessibility.c \
		$(RUNTIME)/build/libscuzz_rt.a $(FFI_SKIA)/build/libsk_capi.a \
		-Wl,--whole-archive $(BUILDDIR)/libscuzz_embedder.a -Wl,--no-whole-archive \
		-lX11 $(DBUS_LIBS) -lpthread -ldl -lssl -lcrypto $(SKIA_LINK) $(GPU_LINK) -o $@

$(BUILDDIR)/test_%: tests/test_%.c $(BUILDDIR)/libscuzz_embedder.a | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $< -L$(BUILDDIR) -lscuzz_embedder -lX11 $(DBUS_LIBS) -o $@

test-asan: dbus-check
	@asan_dir=$$(mktemp -d "$${TMPDIR:-/tmp}/scuzz-embedder-asan.XXXXXX") || exit 1; \
	trap 'rm -rf "$$asan_dir"' 0; \
	if ! printf '%s\n' 'int main(void){return 0;}' | $(CC) $(ASAN_CFLAGS) -x c - -o "$$asan_dir/probe" 2>/dev/null; then \
		echo "embedder-desktop test-asan: AddressSanitizer not supported by $(CC); skipping"; exit 0; \
	fi
	+@$(MAKE) --no-print-directory -C $(RUNTIME) lib-ui-asan CC=$(CC)
	mkdir -p build/asan
	$(CC) $(ASAN_CFLAGS) -UNDEBUG $(INCLUDES) tests/test_accessibility.c \
		$(RUNTIME)/build/asan/libscuzz_rt.a $(FFI_SKIA)/build/asan/libsk_capi.a \
		-Wl,--whole-archive build/asan/libscuzz_embedder.a -Wl,--no-whole-archive \
		-lX11 $(DBUS_LIBS) -lpthread -ldl -lssl -lcrypto $(SKIA_LINK) $(GPU_LINK) -o build/asan/test_accessibility
	ASAN_OPTIONS=detect_leaks=0 dbus-run-session -- ./build/asan/test_accessibility

test-ubsan: dbus-check
	+@$(MAKE) --no-print-directory lib-ubsan CC=$(CC)
	+@$(MAKE) --no-print-directory -C $(RUNTIME) lib ffi-skia BUILDDIR=build/ubsan-embedder CFLAGS='$(UBSAN_CFLAGS)' CC=$(CC)
	$(CC) $(UBSAN_CFLAGS) -UNDEBUG $(INCLUDES) tests/test_accessibility.c \
		$(RUNTIME)/build/ubsan-embedder/libscuzz_rt.a $(FFI_SKIA)/build/libsk_capi.a \
		-Wl,--whole-archive build/ubsan/libscuzz_embedder.a -Wl,--no-whole-archive \
		-lX11 $(DBUS_LIBS) -lpthread -ldl -lssl -lcrypto $(SKIA_LINK) $(GPU_LINK) -o build/ubsan/test_accessibility
	dbus-run-session -- ./build/ubsan/test_accessibility

test: lib $(BUILDDIR)/test_headless $(BUILDDIR)/test_idle_events $(BUILDDIR)/test_resize $(BUILDDIR)/test_motion $(BUILDDIR)/test_accessibility
	dbus-run-session -- ./$(BUILDDIR)/test_motion
	dbus-run-session -- ./$(BUILDDIR)/test_accessibility
	+@$(MAKE) --no-print-directory test-asan CC=$(CC)
	+@$(MAKE) --no-print-directory test-ubsan CC=$(CC)
	env -u DISPLAY ./$(BUILDDIR)/test_headless
	@if [ -n "$$DISPLAY" ]; then ./$(BUILDDIR)/test_idle_events && ./$(BUILDDIR)/test_resize; else echo "embedder-desktop: skip idle/resize events (no DISPLAY)"; fi

else ifeq ($(UNAME_S),Darwin)

SRC := src/macos_present.m $(RUNTIME)/src/net_apple.m
OBJ := $(BUILDDIR)/macos_present.o $(BUILDDIR)/net_apple.o

$(BUILDDIR)/net_apple.o: $(RUNTIME)/src/net_apple.m $(RUNTIME)/src/net_transport.h $(RUNTIME)/src/rt_util.h $(RUNTIME)/include/scuzz_rt.h | $(BUILDDIR)
	$(CC) $(OBJCFLAGS) $(INCLUDES) -I$(RUNTIME)/src -c $< -o $@

$(BUILDDIR)/%.o: src/%.m include/scuzz_embedder.h | $(BUILDDIR)
	$(CC) $(OBJCFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/test_macos_events: tests/test_macos_events.m $(BUILDDIR)/libscuzz_embedder.a | $(BUILDDIR)
	$(CC) $(OBJCFLAGS) -UNDEBUG $(INCLUDES) tests/test_macos_events.m -L$(BUILDDIR) \
		-lscuzz_embedder -framework Cocoa -framework CoreGraphics -o $@

lib: $(BUILDDIR)/libscuzz_embedder.a

lib-asan:
	+@$(MAKE) --no-print-directory lib BUILDDIR=build/asan OBJCFLAGS='$(ASAN_OBJCFLAGS)'

$(BUILDDIR)/libscuzz_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

test: lib $(BUILDDIR)/test_macos_events
	./$(BUILDDIR)/test_macos_events

else

lib:
	@echo "embedder-desktop: skip (Linux/Darwin only; host=$(UNAME_S))"

lib-asan:
	@echo "embedder-desktop: skip (Linux/Darwin only; host=$(UNAME_S))"

test:
	@echo "embedder-desktop: skip (Linux/Darwin only)"

endif

clean:
	rm -rf build
