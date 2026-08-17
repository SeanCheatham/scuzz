# Short-term plan

## Slice: Drop leftover Fs.write pack retains

`Ref.set` / `Queue.offer` / `Deferred.complete` keep the payload in a pair. Unused delay drops leftover retains. `Fs.write` still uses a malloc pack that holds path and contents. Last-use `sz_free`s the pack and leaks those strings.

- Keep path and contents in a pair (same shape as `Ref.set`). Last-use of the delay node drops leftover retains. Steal on run so the thunk can use the strings.
- Proof: `test_io` unused `sz_fs_write` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak. Other malloc delay packs (net, exec).
