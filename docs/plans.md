# Short-term plan

## Slice: GPU presenter path

iOS simulator packaging, TextField input, and the Android NDK `.so` link are in. Only the CPU raster path exists (`sk_sw` or the pinned Skia CPU prebuilt).

- Add one GPU presenter behind `sk_capi` that keeps the same `Ui` session and structural goldens.
- Proof: `examples/counter` Headless structural dump matches the CPU path. Pixel goldens stay within the existing tolerance when `--pixels` is on.
- Out of scope: Android APK/Activity, real devices, OS IME candidate windows.
