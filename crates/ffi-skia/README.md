# ffi-skia

Thin Skia-shaped C ABI (`include/sk_capi.h`) for Headless/Window paint.

**Default:** pinned Skia CPU prebuilt via `scripts/fetch_skia.sh` /
`third_party/skia/PIN` → `build/sk_capi_backend` = `skia`.  
**Opt out:** `SCUZZ_SKIA=sk_sw` builds in-tree `src/sk_sw.c` (see `docs/vision.md`).

```bash
make -C crates/ffi-skia test
# offline / exotic host:
SCUZZ_SKIA=sk_sw make -C crates/ffi-skia test
```
