# Short-term plan

## Slice: Drop the `Queue.take` result as an owned transfer

`sz_deferred_get` retains the completed value so the run result does not alias the Deferred slot. `Queue.take` already holds the offer retain after it removes the item. The compiler still treats the take binder as borrowed, so that ref leaks.

- Mark `Queue.take` payload owned so a last-use drops the transferred value.
- Do not retain again in take. The offer retain is the take result.
- Proof: `test_io` can drop the take result; compiler IR drops an owned `Queue.take` binder.
- Out of scope: OS threads. Making Queue RC. Releasing remaining queued values when the queue frees.
