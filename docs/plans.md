# Short-term plan

## Slice: Drop owned inner IO after IO.ensure

`IO.timeout` retains inner and drops the caller ref. `IO.ensure` stores inner and finalizer without retain, so those graphs can alias the ensure node.

- Retain inner and finalizer in `sz_io_ensure`.
- Drop the caller refs after the call.
- Proof: compiler IR for `IO.ensure(IO.pure("ok"), IO.println("fin"))` shows `sz_release` of inner and finalizer after the call.
- Out of scope: OS threads.
