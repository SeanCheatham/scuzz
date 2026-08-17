# Short-term plan

## Slice: Drop owned inner IO after IO.timeout

`Fiber.fork` retains inner and drops the caller ref. `IO.timeout` stores inner without retain, so the inner graph can alias the timeout node.

- Retain inner in `sz_io_timeout`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.timeout(50, IO.pure("ok"))` shows `sz_release` of the inner after the call.
- Out of scope: OS threads.
