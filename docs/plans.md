# Short-term plan

## Slice: Drop leftover Fs.read unused path retains

`Random.nextInt` keeps the bound in an RC box as delay env. Unused nextInt drops the pack. `Fs.read` keeps the path as flatMap env. Last-use of unused read should drop leftover path retains.

- Keep the path in a pair until dispatch. Last-use of the unused IO drops leftover retains. Steal on run so the thunk can `sz_release` the pack.
- Proof: `test_io` unused `sz_fs_read` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
