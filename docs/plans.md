# Short-term plan

## Slice: Retain error in IO.fail

`IO.pure` retains the payload and drops an owned payload after the call. `IO.fail` still stores the error without retain, so the error can alias the fail node.

- Retain the error in `sz_io_fail`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))` shows `sz_release` of the error after `sz_io_fail` / `sz_io_fail_cstr`.
- Out of scope: OS threads. Releasing the error when the fail node frees (fiber_fail takes it).
