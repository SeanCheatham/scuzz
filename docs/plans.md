# Short-term plan

## Slice: Android APK shell

The NDK link emits `libscuzz.so`. There is no Activity that presents frames on an emulator or device.

- Add a minimal Android Activity + SurfaceView that loads `libscuzz.so` and blits `sz_mobile_present` frames.
- Proof: `scuzz package --target android examples/counter` plus the platform SDK installs and shows one frame on an emulator, or fails with one install line when `adb` / the SDK is missing.
- Out of scope: real-device provisioning, Impeller, OS IME candidate windows.
