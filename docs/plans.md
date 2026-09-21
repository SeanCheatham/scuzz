# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Fault surface** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

`scuzz fuzz --iterations 16 examples/io` fails on both engines on an injected `Fs` fault that CI does not reach at `--iterations 2`. Claims in `examples/webhook` and `examples/api-report` accept `faulted` as a pass. TestRuntime picks the faults it injects without a declaration. Close that:

1. The scenario declares its fault surface (which kinds, on which calls).
2. A claim must not pass on `faulted` alone. Drop `faulted` as a pass in webhook and api-report claims.
3. `examples/io` at `--iterations 16` is green, or fails on a real invariant.

Fault storms and partial writes follow later.

Proof: `examples/io` at `--iterations 16` is green or fails on a real invariant; `examples/webhook` claims no longer pass on `faulted` alone.

Status: not started.
