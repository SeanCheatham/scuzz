# Plain = so a command-line CC=gcc still wins. ?= would lose to make's built-in CC=cc default.
CC = clang
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ASAN_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer
BUILDDIR ?= build
ROOT := $(abspath ../..)
RUNTIME := $(ROOT)/crates/runtime
INCLUDES := -Iinclude -I$(RUNTIME)/include
SRC := src/host_shell.c
OBJ := $(SRC:src/%.c=$(BUILDDIR)/%.o)

.PHONY: all clean lib lib-asan test

all: lib

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/%.o: src/%.c include/scuzz_mobile.h | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: $(BUILDDIR)/libscuzz_mobile.a

lib-asan:
	+@$(MAKE) --no-print-directory lib BUILDDIR=build/asan CFLAGS='$(ASAN_CFLAGS)'

$(BUILDDIR)/libscuzz_mobile.a: $(OBJ)
	ar rcs $@ $(OBJ)

$(BUILDDIR)/test_host: tests/test_host.c $(BUILDDIR)/libscuzz_mobile.a | $(BUILDDIR)
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $< -L$(BUILDDIR) -lscuzz_mobile -o $@

test: $(BUILDDIR)/test_host
	./$(BUILDDIR)/test_host
	python3 shells/android/test_net.py

clean:
	rm -rf build
