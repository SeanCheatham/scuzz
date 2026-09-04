# ffi-skia

Thin Skia-shaped C ABI (`include/sk_capi.h`) for Headless/Desktop paint.

**Default:** pinned Skia CPU prebuilt through `scripts/fetch_skia.sh` /
`third_party/skia/PIN` → `build/sk_capi_backend` = `skia`. `make lib` compiles
the in-tree shim into that archive so the C ABI matches `sk_capi.h`.
**Opt out:** `SCUZZ_SKIA=sk_sw` builds in-tree `src/sk_sw.c`.
**GPU presenter:** `SCUZZ_SKIA=gpu` paints with `sk_sw` and presents through
OpenGL (upload + readback). Missing OpenGL fails with one install line.

```bash
make -C crates/ffi-skia test
# offline / exotic host:
SCUZZ_SKIA=sk_sw make -C crates/ffi-skia test
SCUZZ_SKIA=gpu make -C crates/ffi-skia test
```
