# Short-term plan

## Slice: Release leftover delay env when the delay node frees

IO.pure and IO.fail retain a distinct run result. The IO node drops leftover payload or error on free. `sz_io_delay` still stores `env` without retain/release. An unused delay (for example `Ref.of` before run) leaks a RC env.

- `sz_retain` the env at `sz_io_delay` when it is RC. Last `sz_release` of the delay node drops leftover env.
- Thunks that take the env must steal (null the slot) or hold a distinct RC. Do not `sz_release` a non-RC malloc env after the thunk `sz_free`s it.
- Proof: `test_io` unused `sz_ref_of` / delay with a RC env then `sz_release` returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak. Non-RC malloc envs that the thunk never runs.
