# embedder-mobile

OS presentation + input for `UiRuntime.Mobile`. Headless remains the CI peer for goldens.

## Host shell (Linux CI)

`libscuzz_mobile.a` (`src/host_shell.c`) blits frames to stderr diagnostics when
`SCUZZ_MOBILE_SHELL=1`. Linked by Stage 0 when present (`--whole-archive` so strong
symbols override weak stubs in `libscuzz_rt.a`).

- Touch / lifecycle / soft-keyboard: `sz_mobile_push_event` → polled on `pump`
- Without `SCUZZ_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

`scuzz package` copies templates from `shells/`:

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | JNI glue + manifest; wire NDK for device builds |
| iOS | `shells/ios/` | ObjC app delegate; link under Xcode for sim/device |
| host | (this lib) | CI smoke of the Mobile peer |

Same examples run unmodified via `SCUZZ_UI_RUNTIME=mobile`.
