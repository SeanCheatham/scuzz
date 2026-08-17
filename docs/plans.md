# Short-term plan

## Slice: Retain capture env in flatMap

`flatMap` retains inner and drops the caller IO ref. It still takes the capture list without retain, so the env can alias the bind node.

- Retain env in `sz_io_flatmap`.
- Drop the owned pack after the call.
- Release env when the flatMap node frees.
- Proof: compiler IR for `IO.pure("ok").flatMap(_ => IO.println("ok"))` shows `sz_release` of the capture pack after the call.
- Out of scope: `handleErrorWith` env. OS threads. `IO.pure` payloads.
