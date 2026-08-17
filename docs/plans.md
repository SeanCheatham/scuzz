# Short-term plan

## Slice: Drop leftover Sys.getenv unused key retains

`Net.httpGet` live pack is RC so HANDLE last-use drops the pack. `Sys.getenv` keeps the key as delay env. Unused getenv should drop leftover key retains. Prove it.

- Keep the key as the delay env (already RC). Last-use of the unused getenv IO drops leftover key retains.
- Proof: `test_io` unused `sz_sys_getenv` then `sz_release` of the IO returns alloc stats to baseline. A run under TestRuntime also returns to baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
