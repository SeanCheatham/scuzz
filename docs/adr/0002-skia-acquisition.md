# ADR 0002 — Skia acquisition

## Status

Accepted — Phase 1 implemented (software backend + ABI); Phase 6 Impeller eval deferred

## Context

Renderer is Skia behind a thin C ABI. Building Skia from source is heavy for CI and contributors.

## Decision

1. **Phase 0**: no Skia vendored yet; placeholder directory `third_party/skia/` + fetch notes only.
2. **Phase 1**: thin `sk_capi` ABI in `crates/ffi-skia/` with a **CPU software backend** (`sk_sw`) so Headless CI works without multi‑GB Skia trees. `scripts/fetch_skia.sh` installs **prebuilt Skia static libs** when `SCALUI_SKIA_URL` is set (Linux x64 CPU offscreen first).
3. Source builds remain an escape hatch for maintainers, not the default contributor path.
4. **Phase 6 Impeller evaluation**: Impeller remains a viable alternate GPU backend behind the same `sk_capi` canvas API, but is **not** adopted for v0. Reasons: Headless CI and golden PNGs are CPU-raster; Impeller needs a display/GPU stack that fights the Headless-first rule; `sk_sw` already proves the session protocol. Revisit when Window/Mobile GPU presenters need it — swap must not change `Ui` session or goldens’ logical pixels.
5. Callers (Ui session) depend only on `sk_capi.h`; swapping `sk_sw` for linked Skia/Impeller must not change the session protocol.
6. Toolchain/prebuilt distribution stays URL-driven (`SCALUI_SKIA_URL`, optional `SCALUI_SKIA_TRIPLE`); no Maven/CDN hardcoding in-repo.

## Consequences

- Headless Linux CI is green without downloading Skia.
- ABI stability of our C wrapper matters more than tracking Skia tip daily.
- Hosted prebuilts can replace `sk_sw` later without rewriting Headless.
- Impeller is explicitly optional and deferred — not a Phase 6 blocker.
