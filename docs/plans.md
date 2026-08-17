# Short-term plan

## Slice: Retain payload in IO.pure

`handleErrorWith` capture packs retain then drop after the call. `IO.pure` still stores the payload without retain, so the value can alias the IO node.

- Retain payload in `sz_io_pure`.
- Drop the caller ref after the call.
- Proof: compiler IR for `IO.pure("ok").flatMap(_ => IO.println("ok"))` shows `sz_release` of the payload after `sz_io_pure`.
- Out of scope: OS threads. Releasing the payload when the IO node frees (the run result aliases it).
