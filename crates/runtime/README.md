# ScalUI runtime (C)

Minimal native runtime linked into Stage-0 binaries. Design locks: ADR 0001 (GC), 0003 (IO errors), 0004 (`Ui`/`View`).

## Contents

- Alloc wrappers (`su_alloc` / `su_free`), strings, panic, `SuError`
- Builtin `IO` + blessed kit (`Resource`, `Ref`, `Deferred`, `Queue`, race/both/sleep/errors)
- Blessed impurity: `Clock` / `Random` / `Fs` / `Net` / `Sys` / `IO.println` + `TestRuntime` fakes
- `Ui` session (Headless / Window / Mobile peers): View tree, signals, theme, anim, a11y hooks
- Kernel demos: effects / impurity / UI (`runHeadless`, `runCounter`, `runTodo`)

Links `crates/ffi-skia`. Optional: `embedder-desktop`, `embedder-mobile`.

```bash
make -C crates/runtime test
```
