# embedder-mobile

OS presentation and input for `UiRuntime.Mobile`. Headless remains the CI peer for goldens.

## Host shell (Linux CI)

`libscuzz_mobile.a` (`src/host_shell.c`) logs present/pump diagnostics to stderr when
`SCUZZ_MOBILE_SHELL=1` (pixels stay offscreen on the host). Linked when
present (`--whole-archive` so strong symbols override weak stubs in `libscuzz_rt.a`).

- Touch / lifecycle / soft-keyboard: `sz_mobile_push_event` → polled on `pump`
- Without `SCUZZ_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

`scuzz package --target android` copies the Android template. `scuzz package
--target ios` runs `shells/ios/build_sim.sh` and emits a signed simulator
`.app`. Android still needs NDK wiring.

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | Manifest + JNI stub; wire NDK / activity for device |
| iOS | `shells/ios/` | ObjC shell (present / touch / keyboard) + `build_sim.sh`; builds a signed sim `.app` under Xcode |
| host | (this lib) | CI smoke of the Mobile peer |

iOS simulator proof (macOS arm64 + Xcode):

```bash
scuzz package --target ios examples/counter
xcrun simctl install booted examples/counter/build/ios-sim/counter.app
xcrun simctl launch booted dev.scuzz.app
```

The shell owns `main` + `UIApplicationMain`. The app `main` is renamed to
`scuzz_app_main` in a copy of the IR and runs on a worker thread. It mounts
`UiRuntime.Mobile` through `SCUZZ_UI_RUNTIME=mobile` and pumps until
`sz_mobile_alive` returns 0. Frames cross to the main queue as RGBA8888
(`sz_mobile_present`). Touches enter the pump through the same event queue as
the host shell. Soft keyboard: show/hide only (text events are future work).

Same examples run unmodified through `SCUZZ_UI_RUNTIME=mobile`.
