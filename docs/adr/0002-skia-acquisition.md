# ADR 0002 — Skia acquisition

## Status

Accepted — Phase 1 implemented (software backend + ABI)

## Context

Renderer is Skia behind a thin C ABI. Building Skia from source is heavy for CI and contributors.

## Decision

1. **Phase 0**: no Skia vendored yet; placeholder directory `third_party/skia/` + fetch notes only.
2. **Phase 1**: thin `sk_capi` ABI in `crates/ffi-skia/` with a **CPU software backend** (`sk_sw`) so Headless CI works without multi‑GB Skia trees. `scripts/fetch_skia.sh` installs **prebuilt Skia static libs** when `SCALUI_SKIA_URL` is set (Linux x64 CPU offscreen first).
3. Source builds remain an escape hatch for maintainers, not the default contributor path.
4. Impeller stays a later alternate backend behind the same canvas API (Phase 6).
5. Callers (Ui session) depend only on `sk_capi.h`; swapping `sk_sw` for linked Skia must not change the session protocol.

## Consequences

- Headless Linux CI is green without downloading Skia.
- ABI stability of our C wrapper matters more than tracking Skia tip daily.
- Hosted prebuilts can replace `sk_sw` later without rewriting Headless.
