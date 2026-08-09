# Skia (Phase 1+)

Skia is **not vendored in Phase 0**. See `docs/adr/0002-skia-acquisition.md`.

Planned layout:

- Prebuilt static libraries per platform (Linux x64 CPU offscreen first)
- Thin C ABI under `crates/ffi-skia/`
- Fetch script (to be added in Phase 1) rather than requiring contributors to build Skia from source
