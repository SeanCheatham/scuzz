# Short-term plan

## Slice: Drop leftover Sys.read unused n env

`Sys.write` keeps the payload in a pair until dispatch. Unused write drops leftover string retains. `Sys.read` keeps `n` in a malloc flatMap env. Last-use of unused read is `sz_release`, so that pack does not drop.

- Keep `n` in a leftover RC delay or pair env. Last-use of the unused IO drops the pack. Steal on run so the thunk can drop it.
- Proof: `test_io` unused `sz_sys_read` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
