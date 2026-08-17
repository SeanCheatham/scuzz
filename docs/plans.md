# Short-term plan

## Slice: Retain the current value in `sz_deferred_get`

`sz_ref_get` retains the current value so the run result does not alias the Ref slot. `sz_deferred_get` still resumes with the live Deferred slot, so a last-use drop of the run result would free the completed value.

- Retain the completed value when get resumes (parked waiters and the already-complete path).
- Drop the get binder after the body when the payload is owned.
- Proof: `test_io` can drop the get result and still read the Deferred; compiler IR drops an owned `Deferred.get` binder.
- Out of scope: OS threads. Making Deferred RC. Releasing the completed value when the Deferred frees.
