# Skia

Default path does **not** require a Skia tree. Paint goes through `crates/ffi-skia`
(`sk_capi`) with the in-tree CPU software backend (`sk_sw`) that the Makefile links.

```bash
SCUZZ_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz ./scripts/fetch_skia.sh
```

Layout after fetch: `third_party/skia/prebuilt/<triple>/` (override with `SCUZZ_SKIA_TRIPLE`).
Wiring those prebuilts into `crates/ffi-skia/Makefile` is deferred.

See `docs/vision.md` for deferred Impeller.
