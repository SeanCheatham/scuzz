# Skia

Default path does **not** require a Skia tree. Paint goes through `crates/ffi-skia`
(`sk_capi`) with an in-tree CPU software backend for Headless CI.

```bash
SCALUI_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz ./scripts/fetch_skia.sh
```

Layout after fetch: `third_party/skia/prebuilt/<triple>/` (override with `SCALUI_SKIA_TRIPLE`).

See `docs/vision.md` for prebuilts and deferred Impeller.
