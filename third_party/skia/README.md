# Skia (Phase 1+)

Default contributor path does **not** require a Skia tree. ScalUI paints through `crates/ffi-skia` (`sk_capi`), which ships a CPU software backend for Headless CI.

To install hosted prebuilts when available:

```bash
SCALUI_SKIA_URL=https://…/skia-linux-x64-cpu.tar.gz ./scripts/fetch_skia.sh
```

Layout after fetch: `third_party/skia/prebuilt/<triple>/` (override triple with `SCALUI_SKIA_TRIPLE`).

Phase 6: Impeller remains an optional future backend behind the same ABI — not required for v0 (see `docs/adr/0002-skia-acquisition.md`).
