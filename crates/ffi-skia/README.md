# ffi-skia

Thin Skia-shaped C ABI (`include/sk_capi.h`) for Headless/Window paint.

**Default:** CPU software raster + PNG encode (`src/sk_sw.c`).  
**Optional:** `scripts/fetch_skia.sh` with `SCUZZ_SKIA_URL` (see `docs/vision.md`).

```bash
make -C crates/ffi-skia test
```
