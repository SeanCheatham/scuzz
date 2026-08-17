# Short-term plan

## Slice: Retain the initial value in `sz_ref_make`

`sz_pair_new` retains both sides and drops the caller refs after the call. `sz_ref_make` still stores the initial value without retain, so an RC payload can alias the Ref.

- Retain the initial value in `sz_ref_make`.
- Drop the caller ref after the call.
- Proof: `test_io` `Ref` get/set still works; C kits drop via `ref_make_drop` or an equivalent after `sz_ref_make` / `sz_ref_of`.
- Out of scope: OS threads. Making Ref RC. Releasing the current value when the Ref frees.
