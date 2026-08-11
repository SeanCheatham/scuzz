# Skia

Default path does **not** require a Skia tree. Paint goes through `crates/ffi-skia`
(`sk_capi`) with the in-tree CPU software backend (`sk_sw`) that the Makefile links
when no prebuilt is present.

```bash
SCUZZ_SKIA_URL=https://…/skia-{triple}-cpu.tar.gz ./scripts/fetch_skia.sh
# optional: SCUZZ_SKIA_TRIPLE=…  (default from scripts/skia_triple.sh)
```

`{triple}` is replaced with the host triple (e.g. `x86_64-unknown-linux-gnu`,
`aarch64-apple-darwin`). Layout after fetch:
`third_party/skia/prebuilt/<triple>/libsk_capi.a` (plus optional companion `.a`
files). `crates/ffi-skia/Makefile` copies that archive into
`crates/ffi-skia/build/` when present; otherwise it builds `sk_sw`.

**Tarball contract:** a static library exporting every symbol in
`crates/ffi-skia/include/sk_capi.h` (including measure / text-size APIs). No Skia
headers are required by callers — only `sk_capi.h`. Prefer a single fat
`libsk_capi.a` (shim + Skia objects + embedded font). Companion archives in the
same directory are also linked. Linking a real Skia prebuilt may need
`-lstdc++ -lm -lz -lbz2 -lbrotlidec -lbrotlicommon` (Stage 0 / self-host add these
when `build/sk_capi_backend` is `skia`).

**Pin / release:** `third_party/skia/PIN` records the as-needed `skia-cpu-vN`
URL template (`url=…/skia-{triple}-cpu.tar.gz`). `scripts/package_release.sh`
fetches the host-matching asset into the release tree under
`third_party/skia/prebuilt/` so installed Scuzz can link real text without
rebuilding Skia. Source-checkout CI stays on `sk_sw` unless a prebuilt is
fetched. Impeller / GPU presenters stay deferred (`docs/vision.md`).
