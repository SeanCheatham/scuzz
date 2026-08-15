# Short-term plan

## Mobile: iOS simulator proof

Close the iOS half of [`gaps.md`](gaps.md) unknown 1: `examples/counter` runs on an iOS simulator. Xcode is present on the dev host. Android stays blocked on the NDK.

1. ~~Buildable iOS sim shell~~ **Done.** Live Mobile pump loop (`sz_mobile_alive`), ObjC shell under `crates/embedder-mobile/shells/ios/` (present, touch, keyboard show/hide), `build_sim.sh` builds runtime + sk_sw + app `.ll` for `arm64-apple-ios-simulator` into a signed `.app`. Proven: counter mounts `UiRuntime.Mobile` in a booted sim and renders live (screenshot).
2. `scuzz package --target ios` builds the sim app through the CLI instead of copying templates. Bundle id comes from `scuzz.toml`.
3. Soft-keyboard text input (`SZ_INPUT_TEXT_EDIT`) so TextField works on sim.

Device builds (real hardware, provisioning) stay open in `gaps.md`.
