# Desktop presenter: Linux X11 or Darwin Cocoa. Skipped elsewhere (Headless still works).
UNAME_S := $(shell uname -s)
# Plain = so a command-line CC=gcc still wins. ?= would lose to make's built-in CC=cc default.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
OBJCFLAGS ?= -Wall -Wextra -Werror -O2 -fobjc-arc
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
ASAN_OBJCFLAGS := -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer -fobjc-arc
BUILDDIR ?= build
ROOT := $(abspath ../..)
RUNTIME := $(ROOT)/crates/runtime
INCLUDES := -Iinclude -I$(RUNTIME)/include

.PHONY: all clean lib lib-asan test

all: lib

$(BUILDDIR):
	mkdir -p $@

ifeq ($(UNAME_S),Linux)

INCLUDES += $(shell pkg-config --cflags dbus-1 2>/dev/null)

.PHONY: dbus-check
dbus-check:
	@command -v pkg-config >/dev/null 2>&1 && pkg-config --exists dbus-1 || { echo "Install: sudo apt-get install pkg-config libdbus-1-dev" >&2; exit 1; }

$(BUILDDIR)/x11_present.o $(BUILDDIR)/test_motion: | dbus-check

SRC := src/x11_present.c
OBJ := $(BUILDDIR)/x11_present.o

$(BUILDDIR)/%.o: src/%.c include/scuzz_embedder.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: $(BUILDDIR)/libscuzz_embedder.a

lib-asan:
	+@$(MAKE) --no-print-directory lib BUILDDIR=build/asan CFLAGS='$(ASAN_CFLAGS)'

$(BUILDDIR)/libscuzz_embedder.a: $(OBJ)
	ar rcs $@ $(OBJ)

# Keep assertions active when the library uses release flags.
$(BUILDDIR)/test_idle_events $(BUILDDIR)/test_resize: tests/x11_test.h

$(BUILDDIR)/test_%: tests/test_%.c $(BUILDDIR)/libscuzz_embedder.a | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $< -L$(BUILDDIR) -lscuzz_embedder -lX11 $(shell pkg-config --libs dbus-1) -o $@

test: lib $(BUILDDIR)/test_headless $(BUILDDIR)/test_idle_events $(BUILDDIR)/test_resize $(BUILDDIR)/test_motion
	dbus-run-session -- ./$(BUILDDIR)/test_motion
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
