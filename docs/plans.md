# Short-term plan

## Slice: Drop Queue, Ref, and Deferred cells after last use

`sz_queue_free` / `sz_ref_free` / `sz_deferred_free` now release leftover payloads. Compiled programs still leak the cells: `Queue.unbounded`, `Ref.of`, and `Deferred.empty` mark the handle borrowed, so last-use never frees it.

- Mark those IO payloads owned so a last-use drops the handle (`sz_queue_free` / `sz_ref_free` / `sz_deferred_free`).
- Do not retain the handle again. The constructor result is the owned cell.
- Proof: compiler IR releases the binder after last use; `test_io` already proves free drops leftover payloads.
- Out of scope: OS threads. Making Queue RC. Releasing Either or pair fields.
