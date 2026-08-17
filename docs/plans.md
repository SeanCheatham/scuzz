# Short-term plan

## Slice: Retain both sides in `sz_pair_new`

`sz_either_right` retains the value and drops the caller ref after the call. `sz_pair_new` still stores left and right without retain, so RC payloads can alias the pair.

- Retain both sides in `sz_pair_new`.
- Drop the caller refs after the call.
- Proof: `test_io` `IO.both` still yields a pair; C kits drop via `pair_drop` or an equivalent after `sz_pair_new`.
- Out of scope: OS threads. Making Pair RC. Releasing pair fields when the pair frees (the run result aliases them).
