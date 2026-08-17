# Short-term plan

## Slice: Drop owned inner IO after IO.retryN

`IO.forever` / `IO.repeatN` retain inner and drop the caller ref. `IO.retryN` stores inner without retain, so the inner graph can alias the loop node.

- Retain inner in `sz_io_retry_n`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.retryN(1, IO.pure("ok"))` shows `sz_release` of the inner after the call.
- Out of scope: OS threads.
