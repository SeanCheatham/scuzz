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

`{triple}` is replaced with the host triple (for example `x86_64-unknown-linux-gnu`,
`aarch64-apple-darwin`). Layout after fetch:
`third_party/skia/prebuilt/<triple>/libsk_capi.a`.
`crates/ffi-skia/Makefile` copies that archive into
`crates/ffi-skia/build/` (marker `build/sk_capi_backend` = `skia`).

**Tarball contract:** a static library exporting every symbol in
`crates/ffi-skia/include/sk_capi.h` (including measure / text-size APIs). Callers
need no Skia headers — only `sk_capi.h`. One fat `libsk_capi.a` (shim + Skia
objects + embedded font). Linking needs `-lstdc++`/`-lc++` `-lm -lz -lbz2`
(`scuzz` adds these when `build/sk_capi_backend` is `skia`). On Darwin also link
CoreFoundation / CoreGraphics / CoreText / Foundation / Carbon. The packer turns
WOFF2 off and fails if the fat archive has undefined Brotli symbols. On Linux
install `zlib1g-dev libbz2-dev`.

**Pin / release:** `third_party/skia/PIN` records the as-needed `skia-cpu-vN`
URL template (`url=…/skia-{triple}-cpu.tar.gz`). `scripts/package_release.sh`
fetches the host-matching asset into the release tree (unless
`SCUZZ_SKIA=sk_sw`). Impeller / GPU presenters stay deferred (`docs/vision.md`).
