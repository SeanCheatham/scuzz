# Skia

Default UI path uses the **pinned Skia CPU prebuilt** (no vendored Skia tree).
Paint goes through `crates/ffi-skia` (`sk_capi`). `scripts/fetch_skia.sh` reads
`third_party/skia/PIN` (or `SCUZZ_SKIA_URL`) and installs under
`third_party/skia/prebuilt/<triple>/`. Fail closed if the pin cannot be
satisfied. Opt out with `SCUZZ_SKIA=sk_sw` for the in-tree software backend.

```bash
./scripts/fetch_skia.sh
# or: SCUZZ_SKIA_URL=https://…/skia-{triple}-cpu.tar.gz ./scripts/fetch_skia.sh
# optional: SCUZZ_SKIA_TRIPLE=…  (default from scripts/skia_triple.sh)
# opt out:  SCUZZ_SKIA=sk_sw make -C crates/ffi-skia lib
```

`{triple}` is replaced with the host triple (e.g. `x86_64-unknown-linux-gnu`,
`aarch64-apple-darwin`). Layout after fetch:
`third_party/skia/prebuilt/<triple>/libsk_capi.a` (plus optional companion `.a`
files). `crates/ffi-skia/Makefile` copies that archive into
`crates/ffi-skia/build/` (marker `build/sk_capi_backend` = `skia`).

**Tarball contract:** a static library exporting every symbol in
`crates/ffi-skia/include/sk_capi.h` (including measure / text-size APIs). No Skia
headers are required by callers — only `sk_capi.h`. Prefer a single fat
`libsk_capi.a` (shim + Skia objects + embedded font). Companion archives in the
same directory are also linked. Linking the Skia prebuilt needs
`-lstdc++`/`-lc++` `-lm -lz -lbz2 -lbrotlidec -lbrotlicommon` (`scuzz`
adds these when `build/sk_capi_backend` is `skia`). On Darwin also link
CoreFoundation / CoreGraphics / CoreText / Foundation / Carbon. On Linux install
`zlib1g-dev libbz2-dev libbrotli-dev`; on macOS, Homebrew `brotli` / `bzip2` if
the linker cannot find them.

**Pin / release:** `third_party/skia/PIN` records the as-needed `skia-cpu-vN`
URL template (`url=…/skia-{triple}-cpu.tar.gz`). `scripts/package_release.sh`
fetches the host-matching asset into the release tree (unless
`SCUZZ_SKIA=sk_sw`). Impeller / GPU presenters stay deferred (`docs/vision.md`).
