# Short-term plan

## Slice: Drop leftover Sys.write unused string retains

`Sys.spawn` keeps the command in a pair until start. Unused spawn drops leftover command retains. `Sys.write` keeps the payload as delay env. Last-use of unused write should drop leftover string retains.

- Keep the payload in a pair until dispatch. Last-use of the unused IO drops leftover retains. Steal on run so the thunk can `sz_release` the pack.
- Proof: `test_io` unused `sz_sys_write` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
