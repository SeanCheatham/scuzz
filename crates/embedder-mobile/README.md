# embedder-mobile

OS presentation + input for `UiRuntime.Mobile`. Headless remains the CI peer for goldens.

## Host shell (Linux CI)

`libscalui_mobile.a` (`src/host_shell.c`) blits frames to stderr diagnostics when
`SCALUI_MOBILE_SHELL=1`. Linked by Stage 0 when present (`--whole-archive` so strong
symbols override weak stubs in `libscalui_rt.a`).

- Touch / lifecycle / soft-keyboard: `su_mobile_push_event` → polled on `pump`
- Without `SCALUI_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

`scalui package` copies templates from `shells/`:

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | JNI glue + manifest; wire NDK for device builds |
| iOS | `shells/ios/` | ObjC app delegate; link under Xcode for sim/device |
| host | (this lib) | CI smoke of the Mobile peer |

Same examples run unmodified via `SCALUI_UI_RUNTIME=mobile`.
