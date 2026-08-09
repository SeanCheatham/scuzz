# embedder-mobile (Phase 5)

OS presentation + input for `UiRuntime.Mobile`. Headless remains the CI peer for
goldens; this crate supplies the Mobile interpreter shell.

## Host shell (Linux CI)

`libscalui_mobile.a` (`src/host_shell.c`) blits frames to stderr diagnostics when
`SCALUI_MOBILE_SHELL=1`. Linked by Stage 0 when the archive is present
(`--whole-archive` so strong symbols override weak stubs in `libscalui_rt.a`).

- Touch / lifecycle / soft-keyboard events: `su_mobile_push_event` → polled on
  `pump` via `su_mobile_poll_event`
- Without `SCALUI_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

Templates under `shells/` are emitted by `scalui package`:

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | JNI glue + manifest stub |
| iOS | `shells/ios/` | ObjC app delegate stub |
| host | (this lib) | CI / desktop smoke of the Mobile peer |

Same examples (`counter`, `todo`, …) run unmodified via `SCALUI_UI_RUNTIME=mobile`.
