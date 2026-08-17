# Short-term plan

## Slice: Drop owned inner IO after IO.repeatN

`IO.forever` retains inner and drops the caller ref. `IO.repeatN` stores inner without retain, so the inner graph can alias the loop node.

- Retain inner in `sz_io_repeat_n`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.repeatN(2, IO.pure("ok"))` shows `sz_release` of the inner after the call.
- Out of scope: `IO.retryN`. OS threads.
