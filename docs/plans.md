# Short-term plan

## Slice: Release leftover Either and pair payloads on free

Queue / Ref / Deferred cells are RC and last-use drops them. Either and pair still alias their fields: `sz_either_free` / `sz_pair_free` do not release payloads.

- Make Either and pair RC, or release fields on free and keep the run result as the owner.
- Do not alias fields after free. Getters that need a distinct RC retain first.
- Proof: `test_io` both/attempt then free returns alloc stats to baseline; compiler last-use drops an owned `IO.both` / `attempt` binder when it applies.
- Out of scope: OS threads. Fiber.join retain (join still aliases the fiber result).
