# ScalUI runtime (C)

Minimal native runtime linked into Stage-0 binaries.

## Contents

- Alloc wrappers (`su_alloc` / `su_free`) — GC v0 = libc (ADR 0001)
- UTF-8 strings, panic, `SuError`
- **IO fiber skeleton**: `pure` / `delay` / `flatMap` / `fail` / `println`, trampolined `su_io_unsafe_run`
- **Phase 3 blessed kit**: `Resource` (releases on failure), `Ref`, `Deferred`, `Queue`, `handleErrorWith` / `attempt`, `sleep`, `race` / `both`
- Kernel demo: `su_effects_run_kit`
- **Ui session**: `su_ui_mount` / `pump` / `inject` / `snapshot` for Headless + Window + Mobile peers
- **Phase 2 declarative UI**: View tree, signals, theme tokens, layout, hit testing, IO→UI bridge
- **Phase 5 input**: pointer phases, scroll, soft keyboard visibility, app lifecycle
- Kernel demos: `su_ui_run_headless_label`, `su_ui_run_counter`, `su_ui_run_todo`
- `@main` helper: `su_runtime_main`

Links against `crates/ffi-skia` (`libsk_capi.a`). Optional: `embedder-desktop` (Window), `embedder-mobile` (Mobile).

## Build / test

```bash
make -C crates/runtime test
```
