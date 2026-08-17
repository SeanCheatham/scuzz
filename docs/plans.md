# Short-term plan

## Slice: Release leftover Queue, Ref, and Deferred payloads on free

`Queue.take` now transfers the offer retain. Items that stay in the queue still leak when the queue frees. Ref and Deferred keep the same leftover-payload leak.

- `sz_queue_free` releases remaining items. Do not retain again.
- Add `sz_ref_free`. Release the current value, then free the cell.
- `sz_deferred_free` already drops a failed error. Also release a completed value.
- Proof: `test_io` offers without take, then frees; get after Ref/Deferred free is out of scope. Alloc accounting stays flat across offer-then-free.
- Out of scope: OS threads. Making Queue RC. Releasing Either or pair fields (those still alias the run result).
