# Short-term plan

## Slice: Drop owned inner IO after flatMap

`handleErrorWith` retains inner and drops the caller ref. `flatMap` stores inner without retain, so the inner graph can alias the bind node.

- Retain inner in `sz_io_flatmap`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.pure("ok").flatMap(_ => IO.println("ok"))` shows `sz_release` of the inner after the call.
- Out of scope: OS threads.
