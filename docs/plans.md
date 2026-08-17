# Short-term plan

## Slice: Drop leftover Random.nextInt unused bound env

`Sys.read` keeps n in an RC box as flatMap env. Unused read drops the pack. `Random.nextInt` keeps the bound in a malloc delay env. Last-use of unused nextInt should `sz_free` that pack.

- Keep the bound in a leftover `sz_alloc` delay env. Last-use of the unused IO drops the pack. Steal on run so the thunk can `sz_free` it.
- Proof: `test_io` unused `sz_random_next_int` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
