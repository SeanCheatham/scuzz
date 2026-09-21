# Plans

Edit this file when the short-term slice changes. Delete this file when the slice is done.

## Current slice

Verification arc slice 1: **Claim reachability** ([`vision.md`](vision.md#verification-arc), rank 1 in [`gaps.md`](gaps.md#verification)).

A `Verdict` claim that guards on `driveHas`, `a11yHas`, or `signalStrHas` stays true on every state when the name never matches. Close that:

1. `check` validates `driveHas` / `a11yHas` / `signalStrHas` names against the driver registry and dump slots.
2. A campaign fails when a claim antecedent never fires.

Proof: `examples/bad-*` for a renamed driver; `examples/webhook` claims still pass.

Status: not started.
