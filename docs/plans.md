# Short-term plan

## Slice: Release leftover non-RC delay env when the delay node frees

Delay retains a RC env. Run steals env. Last-use drops leftover RC env. A non-RC `sz_alloc` env (for example `Ref.set`) still leaks if the delay node frees before the thunk `sz_free`s it. `sz_release` is a no-op for that env. Do not `sz_free` a string literal or a small integer env.

- Free leftover env on delay destructor when it is a `sz_alloc` payload and not RC. Steal on run so the thunk can `sz_free` without a double free.
- Proof: `test_io` unused `sz_ref_set` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak. String-literal delay env.
