# Short-term plan

## Slice: Drop owned inner IO after IO.forever

`Resource.make` retains acquire and drops the caller ref. `IO.forever` stores inner without retain, so the inner graph can alias the loop node.

- Retain inner in `sz_io_forever`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.forever(IO.sleep(1))` shows `sz_release` of the inner after the call.
- Out of scope: `IO.repeatN` / `IO.retryN`. OS threads.
