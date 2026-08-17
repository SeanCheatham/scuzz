# Short-term plan

## Slice: Drop leftover Sys.spawn unused command retains

`Sys.alive` / `Sys.kill` keep the pid in a leftover `sz_alloc` delay env. Unused alive/kill drops the pack. `Sys.spawn` keeps the command as delay env. Last-use of unused spawn should drop leftover command retains.

- Keep the command in a pair until start. Last-use of the unused IO drops leftover retains. Steal on run so the thunk can `sz_release` the pack.
- Proof: `test_io` unused `sz_sys_spawn` then `sz_release` of the IO returns alloc stats to baseline. A live run also returns to baseline after the pid box is dropped.
- Out of scope: OS threads. Panic leak. TestRuntime spawn (it fails).
