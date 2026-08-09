# ScalUI runtime (C)

Minimal native runtime linked into Stage-0 binaries.

## Contents

- Alloc wrappers (`su_alloc` / `su_free`) — GC v0 = libc (ADR 0001)
- UTF-8 strings, panic, `SuError`
- **IO fiber skeleton**: `pure` / `delay` / `flatMap` / `fail` / `println`, trampolined `su_io_unsafe_run`
- **Resource**: acquire/release via `su_resource_use`
- **Ui session** (Phase 1): `su_ui_mount` / `pump` / `inject` / `snapshot` for Headless + Window peers; `su_ui_run_headless_label` for kernel dialect
- `@main` helper: `su_runtime_main`

Links against `crates/ffi-skia` (`libsk_capi.a`).

## Build / test

```bash
make -C crates/runtime test
```
