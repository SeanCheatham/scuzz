# Short-term plan

## Slice: Android emulator touch and TextField

The Android APK blits `sz_mobile_present` frames. Touches and typed text do not reach the pump.

- Forward SurfaceView taps into `sz_mobile_push_event`. Show a hidden `EditText` on TextField focus and map insert/backspace to `SZ_INPUT_TEXT_EDIT` (same contract as the iOS shell).
- Proof: `examples/counter` or `examples/studio` on a booted emulator registers one tap or one typed edit, or fails with one install line when `adb` is missing.
- Out of scope: real-device provisioning, Impeller, OS IME candidate windows.
