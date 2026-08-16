# Short-term plan

## Slice: IO last-use

Core value types are in (`Float`, `Map` / `Set`). Long-lived IO still holds graphs until process exit.

- Emit retain/release on IO handles so `flatMap` / `println` / `pure` graphs drop after `unsafe_run`. Panic may still leak.
- Proof: `examples/kernel` and `examples/io` still pass `scuzz check` and `scuzz test`. Alloc accounting on a counter-shaped Headless pump stays flat.
- Out of scope: OS threads, cycle collection.
