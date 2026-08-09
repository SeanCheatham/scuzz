# ffi-skia (Phase 1)

Thin Skia-shaped C ABI (`include/sk_capi.h`) for ScalUI Headless/Window paint.

**Default backend:** CPU software raster + PNG encode (`src/sk_sw.c`).  
**Optional:** `scripts/fetch_skia.sh` with `SCALUI_SKIA_URL` for prebuilt Skia (ADR 0002).

```bash
make -C crates/ffi-skia test
```
