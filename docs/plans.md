# Short-term plan

## Slice: Mobile on one real device

iOS simulator and Android emulator present, tap, and type. Hardware provisioning is still open.

- Install one signed iOS device `.app` / `.ipa` or one Android APK on a physical phone and show one frame (or one tap).
- Proof: `scuzz package` plus the platform toolchain runs `examples/counter` on that device, or fails with one install line when the device toolchain is missing.
- Out of scope: Impeller, OS IME candidate windows, store signing.
