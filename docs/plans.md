# Short-term plan

## Slice: Transfer the IO.fail error so fiber_fail does not alias the IO slot

IO.pure retains a distinct run result. The IO node drops leftover payload on free. `IO.fail` still aliases the error: the IO node does not release it, and `fiber_fail` takes the same pointer.

- Steal or retain so last-use / fiber_fail holds a distinct RC. Release leftover error when the fail node frees.
- Do not alias the error after the node drops. Same pattern as `IO.pure` / `Fiber.join`.
- Proof: `test_io` fail then free returns alloc stats to baseline; unused `sz_io_fail` then `sz_release` returns to baseline.
- Out of scope: OS threads. Panic leak.
