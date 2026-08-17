# Short-term plan

## Slice: Retain the offer value in `sz_queue_offer`

`sz_ref_set` retains the new value and drops the caller ref after the call. `sz_queue_offer` still stores the value without retain, so an RC payload can alias the queue.

- Retain the value in `sz_queue_offer`.
- Drop the caller ref after the call.
- Proof: `test_io` queue offer/take still works; C kits drop via `queue_offer_drop` or an equivalent after `sz_queue_offer`.
- Out of scope: OS threads. Making Queue RC. Releasing queued values when the queue frees.
