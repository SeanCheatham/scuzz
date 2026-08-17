# Short-term plan

## Slice: Retain the new value in `sz_ref_set`

`sz_ref_make` retains the initial value and drops the caller ref after the call. `sz_ref_set` still stores the new value without retain, so an RC payload can alias the Ref. The old value is not released.

- Retain the new value in `sz_ref_set`.
- Drop the caller ref after the call.
- Release the previous value when the set runs.
- Proof: `test_io` Ref get/set still works; after set, a dropped caller string remains readable through `Ref.get`.
- Out of scope: OS threads. Making Ref RC. Releasing the current value when the Ref frees.
