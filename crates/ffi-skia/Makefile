# Plain = so a command-line CC=gcc still wins. ?= would lose to make's built-in CC=cc default.
CC = clang
CXX ?= clang++
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
ROOT := $(abspath ../..)
TRIPLE ?= $(shell $(ROOT)/scripts/skia_triple.sh)
PREBUILT_DIR := $(ROOT)/third_party/skia/prebuilt/$(TRIPLE)
PREBUILT_LIB := $(PREBUILT_DIR)/libsk_capi.a
PIN := $(ROOT)/third_party/skia/PIN
SKIA_BRANCH := $(shell awk -F= '/^skia_branch=/{print substr($$0,13); exit}' "$(PIN)")
# Release asset, not gitiles: the +archive endpoint 503s under load.
SKIA_HDR_URL := $(shell awk -F= '/^hdr_url=/{print substr($$0,9); exit}' "$(PIN)")
INCLUDES := -Iinclude -Isrc
SRC := src/image.c src/image_dec.c src/sk_sw.c src/png_enc.c src/sk_mono.c src/sk_color.c
OBJ := $(SRC:src/%.c=build/%.o)
SHIM_OBJS := build/sk_capi_skia.o build/sk_capi_skia_bridge.o
HB_REV := $(shell awk -F= '/^harfbuzz_revision=/{print $$2; exit}' "$(PIN)")
HB_SHA := $(shell awk -F= '/^harfbuzz_sha256=/{print $$2; exit}' "$(PIN)")
HB_DIR := build/harfbuzz
SB_REV := $(shell awk -F= '/^sheenbidi_revision=/{print $$2; exit}' "$(PIN)")
SB_SHA := $(shell awk -F= '/^sheenbidi_sha256=/{print $$2; exit}' "$(PIN)")
SB_DIR := build/sheenbidi

.PHONY: all clean test lib lib-skia lib-sk-sw lib-gpu ensure-prebuilt ensure-fonts

all: lib

build:
	mkdir -p build

# Default: pinned Skia prebuilt (fetch if needed). Opt out with SCUZZ_SKIA=sk_sw.
# SCUZZ_SKIA=gpu: OpenGL raster. No CPU paint buffer. Peek reads the framebuffer.
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

build/%.o: src/%.c include/sk_capi.h include/sk_text.h src/png_enc.h src/sk_gpu.h src/sk_utf8.h | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/image.o build/image_dec.o: src/image_dec.h

build/image_dec.o: src/stb_image.h

build/sk_gpu.o: src/sk_gpu.c src/sk_gpu.h | build
	$(CC) $(CFLAGS) -Wno-deprecated-declarations $(INCLUDES) -c $< -o $@

# Headers that match the pin's Skia branch. Not a vendored tree.
build/skia-src/.stamp: | build
	@mkdir -p build/skia-src/include
	@if [ -z "$(SKIA_HDR_URL)" ]; then \
		echo "ffi-skia: no hdr_url= in $(PIN)" >&2; \
		exit 1; \
	fi
	@echo "ffi-skia: fetch $(SKIA_BRANCH) include for shim relink"
	@"$(ROOT)/scripts/fetch_skia.sh" --download "$(SKIA_HDR_URL)" build/skia-include.tar.gz
	@rm -rf build/skia-src/include
	@mkdir -p build/skia-src/include
	@tar -xzf build/skia-include.tar.gz -C build/skia-src/include
	@test -f build/skia-src/include/core/SkCanvas.h
	@echo "$(SKIA_BRANCH)" > $@

$(HB_DIR)/.stamp: $(PIN) | build
	@mkdir -p $(HB_DIR)
	@"$(ROOT)/scripts/fetch_skia.sh" --download "https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/$(HB_REV)" build/harfbuzz.tar.gz
	@python3 -c 'import hashlib,pathlib,sys,tarfile; p=pathlib.Path(sys.argv[1]); data=p.read_bytes(); hashlib.sha256(data).hexdigest()==sys.argv[2] or sys.exit("HarfBuzz archive hash differs"); out=pathlib.Path(sys.argv[3]); t=tarfile.open(p); [(out/pathlib.PurePosixPath(m.name).name).write_bytes(t.extractfile(m).read()) for m in t.getmembers() if m.isfile() and len(pathlib.PurePosixPath(m.name).parts)==3 and pathlib.PurePosixPath(m.name).parts[1]=="src" and m.name.endswith(".h")]' build/harfbuzz.tar.gz "$(HB_SHA)" $(HB_DIR)
	@test -f $(HB_DIR)/hb.h
	@test -f $(HB_DIR)/hb-version.h
	@echo "$(HB_REV)" > $@

$(HB_DIR)/.source-stamp: $(HB_DIR)/.stamp
	@if [ ! -f build/harfbuzz.tar.gz ]; then "$(ROOT)/scripts/fetch_skia.sh" --download "https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/$(HB_REV)" build/harfbuzz.tar.gz; fi
	@python3 -c 'import hashlib,pathlib,sys,tarfile; p=pathlib.Path(sys.argv[1]); hashlib.sha256(p.read_bytes()).hexdigest()==sys.argv[2] or sys.exit("HarfBuzz archive hash differs"); out=pathlib.Path(sys.argv[3]); t=tarfile.open(p); [(lambda p,m: (p.parent.mkdir(parents=True,exist_ok=True),p.write_bytes(t.extractfile(m).read())))(out.joinpath(*pathlib.PurePosixPath(m.name).parts[2:]),m) for m in t.getmembers() if m.isfile() and pathlib.PurePosixPath(m.name).parts[1]=="src"]' build/harfbuzz.tar.gz "$(HB_SHA)" $(HB_DIR)/Source
	@test -f $(HB_DIR)/Source/harfbuzz.cc
	@echo "$(HB_REV)" > $@

build/harfbuzz.o: $(HB_DIR)/.source-stamp
	$(CXX) -std=c++17 -O2 -fPIC -c $(HB_DIR)/Source/harfbuzz.cc -o $@

$(HB_DIR)/arabic_joining.h: $(HB_DIR)/.stamp $(ROOT)/scripts/arabic_joining.py
	python3 "$(ROOT)/scripts/arabic_joining.py" build/harfbuzz.tar.gz "$(HB_SHA)" $@

$(HB_DIR)/HarfBuzz-LICENSE: $(HB_DIR)/arabic_joining.h
	@test -f $@ || python3 "$(ROOT)/scripts/arabic_joining.py" build/harfbuzz.tar.gz "$(HB_SHA)" $(HB_DIR)/arabic_joining.h

$(SB_DIR)/.stamp: $(PIN) | build
	@mkdir -p $(SB_DIR)
	@"$(ROOT)/scripts/fetch_skia.sh" --download "https://codeload.github.com/Tehreer/SheenBidi/tar.gz/$(SB_REV)" build/sheenbidi.tar.gz
	@python3 -c 'import hashlib,pathlib,sys,tarfile; p=pathlib.Path(sys.argv[1]); hashlib.sha256(p.read_bytes()).hexdigest()==sys.argv[2] or sys.exit("SheenBidi archive hash differs"); out=pathlib.Path(sys.argv[3]); t=tarfile.open(p); members=[m for m in t.getmembers() if m.isfile() and pathlib.PurePosixPath(m.name).parts[1] in ("Headers","Source","LICENSE")]; [(lambda p,m: (p.parent.mkdir(parents=True,exist_ok=True),p.write_bytes(t.extractfile(m).read())))(out.joinpath(*pathlib.PurePosixPath(m.name).parts[1:]),m) for m in members]' build/sheenbidi.tar.gz "$(SB_SHA)" $(SB_DIR)
	@test -f $(SB_DIR)/Headers/SheenBidi/SheenBidi.h
	@echo "$(SB_REV)" > $@

build/sheenbidi.o: $(SB_DIR)/.stamp
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-unused-function -fPIC -DSB_CONFIG_UNITY -I$(SB_DIR)/Headers -I$(SB_DIR)/Source -c $(SB_DIR)/Source/SheenBidi.c -o $@

build/sk_capi_skia.o: src/sk_capi_skia.cpp src/sk_capi_skia.h src/sk_text_shape.h include/sk_text.h build/skia-src/.stamp $(HB_DIR)/.stamp $(SB_DIR)/.stamp
	$(CXX) -std=c++17 -O2 -fPIC -c src/sk_capi_skia.cpp -o $@ \
	  -Ibuild/skia-src -I$(HB_DIR) -I$(SB_DIR)/Headers -DSK_RELEASE

build/fonts.S: $(ROOT)/scripts/fetch_fonts.py $(ROOT)/third_party/fonts/PIN | build
	python3 "$(ROOT)/scripts/fetch_fonts.py" build/fonts --assembly $@

ensure-fonts: | build
	python3 "$(ROOT)/scripts/fetch_fonts.py" build/fonts

build/fonts.o: build/fonts.S | ensure-fonts
	$(CC) -fPIC -c $< -o $@

build/sk_capi_skia_bridge.o: src/sk_capi_skia_bridge.c include/sk_capi.h include/sk_text.h src/sk_capi_skia.h | build
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
lib-skia: build/fonts.o build/sheenbidi.o build/image.o build/image_dec.o build/sk_gpu_skia_stub.o build/sk_mono.o build/sk_color.o $(SHIM_OBJS) | build
	@test -f "$(PREBUILT_LIB)"
	@if [ -f build/libsk_capi.a ] && [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend \
	    && [ ! "$(PREBUILT_LIB)" -nt build/libsk_capi.a ] \
	    && [ ! build/sk_gpu_skia_stub.o -nt build/libsk_capi.a ] \
	    && [ ! build/sk_mono.o -nt build/libsk_capi.a ] \
	    && [ ! build/sk_color.o -nt build/libsk_capi.a ] \
	    && [ ! build/image_dec.o -nt build/libsk_capi.a ] \
	    && [ ! build/image.o -nt build/libsk_capi.a ] \
	    && [ ! build/fonts.o -nt build/libsk_capi.a ] \
	    && [ ! build/sheenbidi.o -nt build/libsk_capi.a ] \
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
	ar d build/libsk_capi.a sk_capi_skia.o sk_capi_skia_bridge.o sheenbidi.o fonts.o scuzz_embedded_font.o scuzz_embedded_mono_font.o; \
	ar rcs build/libsk_capi.a build/fonts.o build/sheenbidi.o $(SHIM_OBJS) build/image.o build/image_dec.o build/sk_gpu_skia_stub.o build/sk_mono.o build/sk_color.o; \
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

# Test the selected paint archive. Keep assertions active.
build/test_%: tests/test_%.c lib
	@extra=""; \
	if [ -f build/sk_capi_backend ] && grep -qx skia build/sk_capi_backend; then \
	  extra="$(SKIA_LIBS)"; \
	elif [ -f build/sk_capi_backend ] && grep -qx gpu build/sk_capi_backend; then \
	  extra="$(GPU_LIBS)"; \
	fi; \
	$(CC) $(CFLAGS) -UNDEBUG $(INCLUDES) $< -Lbuild -lsk_capi $$extra -lpthread -lm -o $@

test: build/test_skia build/test_sk_clip
	./build/test_skia
	./build/test_sk_clip
	@if [ "$${SCUZZ_SKIA:-}" = "gpu" ]; then \
	  $(MAKE) build/test_sk_gpu; \
	  ./build/test_sk_gpu; \
	fi

clean:
	rm -rf build
