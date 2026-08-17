# Short-term plan

## Slice: Retain the Fiber.join result so join does not alias the fiber slot

Queue / Ref / Deferred / Either / pair cells are RC and last-use drops them. `Fiber.join` still aliases `result_value`. `pure_drop` of that pointer does not give the joiner a distinct RC. Fiber free does not release the slot.

- Retain on complete so join and the fiber each hold a ref. Release `result_value` when the fiber frees.
- Do not alias the slot after free. Join that needs a distinct RC retains first (same pattern as `Ref.get` / `Deferred.get`).
- Proof: `test_io` fork/join then free returns alloc stats to baseline; compiler last-use drops an owned `Fiber.join` binder when it applies.
- Out of scope: OS threads. Supervision trees.
