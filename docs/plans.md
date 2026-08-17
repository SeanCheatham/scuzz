# Short-term plan

## Slice: Drop the handleErrorWith error message

`sz_error_message` returns a fresh string. The compiler stores that binder as borrowed, so the copy leaks after the handler runs.

- Mark the `handleErrorWith` error-message binder owned.
- Drop it after the handler body.
- Proof: compiler IR for `IO.fail("boom").handleErrorWith(e => IO.println(e))` shows `sz_release` of the message after last use.
- Out of scope: OS threads. Releasing the `SzError` itself in the handler.
