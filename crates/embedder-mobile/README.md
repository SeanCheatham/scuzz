# embedder-mobile

OS presentation and input for `UiRuntime.Mobile`. Headless remains the CI peer.

## Host shell (Linux CI)

`libscuzz_mobile.a` (`src/host_shell.c`) logs present and keyboard lines to
stderr when `SCUZZ_MOBILE_SHELL=1` (pixels stay offscreen on the host).
Linked when present (`--whole-archive` on Linux, `-force_load` on Darwin, so
strong symbols override weak stubs in `libscuzz_rt.a`).

- Host shell has no input queue. `sz_mobile_poll_event` returns 0.
  `sz_mobile_alive` returns 0 so the CI smoke stays one frame
- Without `SCUZZ_MOBILE_SHELL`, Mobile stays offscreen (same paint path)

## Packaging shells

| Target | Path | Role |
| --- | --- | --- |
| Android | `shells/android/` | JNI + Activity + SurfaceView + `build_ndk.sh` / `build_apk.sh`; packs a debug APK |
| iOS | `shells/ios/` | ObjC shell (present / touch / keyboard) + `build_sim.sh`; builds a signed sim `.app` under Xcode |
| host | (this lib) | CI smoke of the Mobile peer |

App instructions: `scuzz docs ios` and `scuzz docs android`.
