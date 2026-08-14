# embedder-mobile

OS presentation and input for `UiRuntime.Mobile`. Headless remains the CI peer for goldens.

## Host shell (Linux CI)

`libscuzz_mobile.a` (`src/host_shell.c`) logs present/pump diagnostics to stderr when
`SCUZZ_MOBILE_SHELL=1` (pixels stay offscreen on the host). Linked when
present (`--whole-archive` so strong symbols override weak stubs in `libscuzz_rt.a`).

- Touch / lifecycle / soft-keyboard: `sz_mobile_push_event` → polled on `pump`
- Without `SCUZZ_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

`scuzz package` copies **non-buildable templates** from `shells/` (layout + JNI/ObjC
hooks). Device builds still need NDK/Xcode wiring.

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | Manifest + JNI stub; wire NDK / activity for device |
| iOS | `shells/ios/` | Info.plist + C helpers; wire under Xcode for sim/device |
| host | (this lib) | CI smoke of the Mobile peer |

Same examples run unmodified through `SCUZZ_UI_RUNTIME=mobile`.
