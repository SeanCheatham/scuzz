# Short-term plan

## Slice: Retain the Left error in `sz_either_left`

`sz_error_message` shares the error's message. `sz_either_left` still stores the error without retain, so the error can alias the Either.

- Retain the error in `sz_either_left`.
- Drop the caller ref after the call.
- Proof: `test_io` attempt of `IO.fail` still yields Left; C kits drop via `either_left_drop` or an equivalent after `sz_either_left`.
- Out of scope: OS threads. Making Either itself RC.
