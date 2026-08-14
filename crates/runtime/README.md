# Scuzz Lang runtime (C)

Minimal native runtime linked into Stage-0 / Stage-1 / Stage-2 binaries. Design locks: [`docs/vision.md`](../../docs/vision.md).

## Contents

- Alloc wrappers (`sz_alloc` / `sz_free`) with live byte/count stats (`sz_alloc_stats`), strings, panic, `SzError`
- Builtin `IO` + blessed kit (`Resource`, `Stream`, `Ref`, `Deferred`, `Queue`, `Fiber.fork`/`join`/`interrupt`, race/both/timeout/sleep/errors)
- Blessed impurity: `Clock` / `Random` / `Fs` / `Net` / `Sys` / `IO.println` + `TestRuntime` fakes
- `Ui` session (Headless / Window / Mobile peers): View tree, signals, theme, a11y hooks
- Language FFI: `sz_lang_*` for `Signal` / `View` / `Ui.run`; impurity kit entry `Impurity.runKit`

Links `crates/ffi-skia`. Optional: `embedder-desktop`, `embedder-mobile`.

```bash
make -C crates/runtime test
make -C crates/runtime test-asan   # optional ASan; skipped if unsupported
```
