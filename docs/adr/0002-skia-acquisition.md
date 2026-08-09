# ADR 0002 — Skia acquisition

## Status

Accepted (intent); implementation starts Phase 1

## Context

Renderer is Skia behind a thin C ABI. Building Skia from source is heavy for CI and contributors.

## Decision

1. **Phase 0**: no Skia vendored yet; placeholder directory `third_party/skia/` + fetch notes only.
2. **Phase 1**: prefer **prebuilt Skia static libs per platform** (Linux x64 CPU offscreen first) fetched by a script; thin `sk_capi`-style ABI in `crates/ffi-skia/`.
3. Source builds remain an escape hatch for maintainers, not the default contributor path.
4. Impeller stays a later alternate backend behind the same canvas API (Phase 6).

## Consequences

- Headless Linux CI can cache prebuilts.
- ABI stability of our C wrapper matters more than tracking Skia tip daily.
