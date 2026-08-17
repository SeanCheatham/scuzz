# Short-term plan

## Slice: Drop owned inner IO after handleErrorWith

`IO.both` retains both arms and drops the caller refs. `handleErrorWith` stores inner without retain, so the inner graph can alias the handler node.

- Retain inner in `sz_io_handle_error_with`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))` shows `sz_release` of the inner after the call.
- Out of scope: OS threads. `flatMap` stays transfer until the next slice.
