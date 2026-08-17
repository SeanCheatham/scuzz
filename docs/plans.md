# Short-term plan

## Slice: Retain the Right value in `sz_either_right`

`sz_either_left` retains the error and drops the caller ref after the call. `sz_either_right` still stores the value without retain, so an RC payload can alias the Either.

- Retain the value in `sz_either_right`.
- Drop the caller ref after the call.
- Proof: `test_io` attempt of `IO.pure` still yields Right; C kits drop via `either_right_drop` or an equivalent after `sz_either_right`.
- Out of scope: OS threads. Making Either itself RC. Releasing the Right value when Either frees (the result aliases it).
