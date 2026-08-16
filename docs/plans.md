# Short-term plan

## Slice: iOS sim TextField input

`scuzz package --target ios` builds a signed simulator `.app`. The iOS shell shows and hides the soft keyboard. It does not feed typed text into the session.

- Wire the hidden `UITextField` insert and backspace events to `sz_mobile_push_event` (`SZ_INPUT_TEXT_EDIT`) in `crates/embedder-mobile/shells/ios/ScuzzShell.m`.
- Proof: a TextField in `examples/studio` (or `examples/counter`) accepts typed text in the iOS simulator.
- Out of scope: Android NDK, device provisioning, OS IME candidate windows.
