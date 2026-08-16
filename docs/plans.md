# Short-term plan

## Slice: Android package CLI

iOS simulator packaging and TextField input are in. `scuzz package --target android` still copies a manifest plus JNI stub. It does not cross-compile.

- Make `scuzz package --target android` fail on the first missing NDK tool with one install line, or emit a linked Android package when the NDK is present.
- Proof: `scuzz package --target android examples/counter` exits 0 with an NDK, or fails with one install line when the NDK is missing.
- Out of scope: real devices, GPU presenters, OS IME candidate windows.
