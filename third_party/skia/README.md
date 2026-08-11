# Skia

Default path does **not** require a Skia tree. Paint goes through `crates/ffi-skia`
(`sk_capi`) with the in-tree CPU software backend (`sk_sw`) that the Makefile links.

```bash
SCUZZ_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz ./scripts/fetch_skia.sh
```

Layout after fetch: `third_party/skia/prebuilt/<triple>/` (override with `SCUZZ_SKIA_TRIPLE`).

**Expected tarball contract** (when a hosted artifact exists): a static library (or set of
libs) that exports every symbol declared in `crates/ffi-skia/include/sk_capi.h`, plus any
additive measure/font APIs added to that header. Unpack so the linker can find the archive
under `prebuilt/<triple>/`. No Skia headers are required by callers — only `sk_capi.h`.

Wiring those prebuilts into `crates/ffi-skia/Makefile` stays deferred until such a URL
exists (see `docs/gaps.md` unknown 1 — real text). Impeller / GPU presenters stay deferred
(`docs/vision.md`).
