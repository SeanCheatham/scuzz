# Short-term plan

## Slice: Drop owned inner IO after IO.both

`IO.race` retains both arms and drops the caller refs. `IO.both` stores left and right without retain, so those graphs can alias the both node.

- Retain left and right in `sz_io_both`.
- Drop the caller refs after the call.
- Proof: compiler IR for `IO.both(IO.pure("a"), IO.pure("b"))` shows `sz_release` of both arms after the call.
- Out of scope: OS threads.
