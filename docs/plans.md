# Short-term plan

## Slice: Retain the current value in `sz_ref_get`

`sz_deferred_complete` retains the value and drops the caller ref after the call. `sz_ref_get` still returns the live Ref slot, so a last-use drop of the run result would free the Ref's value.

- Retain the current value in the get thunk so the run result is a distinct ref.
- Proof: `test_io` can drop the get result and still read the Ref; compiler last-use of an owned get binder still typechecks and runs.
- Out of scope: OS threads. Making Ref RC. Releasing the current value when the Ref frees.
