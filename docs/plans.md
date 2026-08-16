# Short-term plan

## Slice: iOS package CLI

Core value types and IO last-use are in. `scuzz package --target ios` still copies templates. The CLI does not drive `build_sim.sh`.

- Make `scuzz package --target ios` run the iOS simulator shell (`crates/embedder-mobile/shells/ios/build_sim.sh`) for `examples/counter`.
- Proof: `scuzz package --target ios examples/counter` exits 0 on a host that has the iOS simulator toolchain, or fails with one install line when the toolchain is missing.
- Out of scope: Android NDK, device provisioning, sim TextField input.
