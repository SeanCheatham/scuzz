# Short-term plan

## Slice: Drop leftover Queue.offer and Deferred.complete pack retains

Delay last-use drops leftover RC env or a leftover `sz_alloc` env. `Ref.set` keeps the new value in a pair. Unused `Queue.offer` and `Deferred.complete` still use a malloc pack that holds extra retains. Last-use `sz_free`s the pack and leaks those retains.

- Keep the offer or complete payload in a pair (same shape as `Ref.set`). Last-use of the delay node drops leftover retains. Steal on run so the thunk can take the value.
- Proof: `test_io` unused `sz_queue_offer` / `sz_deferred_complete` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak. Other malloc delay packs (`Fs.write`, net, exec).
