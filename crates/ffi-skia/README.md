# ffi-skia

Thin Skia-shaped C ABI (`include/sk_capi.h`) for Headless/Window paint.

**Default (linked today):** CPU software raster + PNG encode (`src/sk_sw.c`).  
**Reserved:** `scripts/fetch_skia.sh` with `SCUZZ_SKIA_URL` installs under
`third_party/skia/prebuilt/<triple>/`. The Makefile prefers that `libsk_capi.a` when
present; otherwise it builds `sk_sw` (see `docs/vision.md`, `docs/gaps.md`).

```bash
make -C crates/ffi-skia test
```
