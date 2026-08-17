# Short-term plan

## Slice: Retain capture env in handleErrorWith

The compiler retains a `flatMap` capture list and drops the pack after the call. `handleErrorWith` still takes the capture list without retain, so the env can alias the handler node.

- Retain the capture pack before `sz_io_handle_error_with`.
- Drop the owned pack after the call.
- Proof: compiler IR for `IO.fail("boom").handleErrorWith(_ => IO.println("recovered"))` shows `sz_release` of the capture pack after the call.
- Out of scope: OS threads. `IO.pure` payloads. Runtime env retain (C kits pass non-list env).
