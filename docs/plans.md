# Short-term plan

## Slice: Drop an owned IO.pure payload on last-use

Fiber.join retains a distinct result. The fiber slot keeps its own ref until free. `IO.pure` still aliases the payload: the IO node does not release it, and the compiler does not mark the payload owned.

- Retain so last-use of an `IO.pure` binder can drop the payload without freeing a live IO slot, or transfer the retain to the run result and drop the node payload.
- Do not alias the payload after the binder drops. Same pattern as `Ref.get` / `Fiber.join`.
- Proof: `test_io` pure then last-use / free returns alloc stats to baseline; compiler last-use drops an owned `IO.pure` binder when it applies (`_ <- IO.pure("x")` / named bind).
- Out of scope: OS threads. Panic leak.
