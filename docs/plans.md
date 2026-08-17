# Short-term plan

## Slice: Drop owned inner IO after Fiber.fork

`IO.retryN` retains inner and drops the caller ref. `Fiber.fork` stores inner without retain, so the child graph can alias the fork node.

- Retain inner in `sz_fiber_fork`.
- Drop the caller ref after the call.
- Proof: compiler IR for `Fiber.fork(IO.sleep(1))` shows `sz_release` of the inner after the call.
- Out of scope: OS threads.
