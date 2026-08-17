# Short-term plan

## Slice: Retain the complete value in `sz_deferred_complete`

`sz_queue_offer` retains the value and drops the caller ref after the call. `sz_deferred_complete` still stores the value without retain, so an RC payload can alias the Deferred.

- Retain the value in `sz_deferred_complete`.
- Drop the caller ref after the call.
- Proof: `test_io` Deferred complete/get still works; C kits drop via `deferred_complete_drop` or an equivalent after `sz_deferred_complete`.
- Out of scope: OS threads. Making Deferred RC. Releasing the completed value when the Deferred frees.
