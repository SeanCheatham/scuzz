# Short-term plan

## Slice: Drop owned inner IO after IO.attempt

`flatMap` retains inner and drops the caller ref. `IO.attempt` stores inner without retain, so the inner graph can alias the attempt node.

- Retain inner in `sz_io_attempt`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.fail("boom").attempt` shows `sz_release` of the inner after the call.
- Out of scope: OS threads.
