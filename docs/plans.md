# Short-term plan

## Slice: Share the error message in `sz_error_message`

`handleErrorWith` drops the binder after the body. `sz_error_message` still copies bytes into a new string, so each recovery pays an extra alloc.

- Retain `err->message` and return it. Keep a fresh string when `err` is null.
- Proof: `IO.fail("boom").handleErrorWith(e => IO.println(e))` still typechecks and emits `sz_release` of the message; `test_io` handleErrorWith still recovers.
- Out of scope: OS threads. Mutating error messages.
