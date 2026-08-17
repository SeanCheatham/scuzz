# Short-term plan

## Slice: Drop leftover Sys.alive / Sys.kill unused pid env

`Sys.getenv` keeps the key in a pair until run. Unused getenv drops leftover key retains. `Sys.alive` / `Sys.kill` keep the pid in a malloc delay env. Last-use of unused alive/kill should `sz_free` that pack.

- Keep the pid in a leftover `sz_alloc` delay env. Last-use of the unused IO drops the pack. Steal on run so the thunk can `sz_free` it.
- Proof: `test_io` unused `sz_sys_alive` / `sz_sys_kill` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
