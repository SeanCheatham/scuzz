# Short-term plan

## Slice: Drop owned inner IO after IO.race

`IO.ensure` retains inner and finalizer and drops the caller refs. `IO.race` stores left and right without retain, so those graphs can alias the race node.

- Retain left and right in `sz_io_race`.
- Drop the caller refs after the call.
- Proof: compiler IR for `IO.race(IO.sleep(1), IO.pure("ok"))` shows `sz_release` of both arms after the call.
- Out of scope: OS threads.
