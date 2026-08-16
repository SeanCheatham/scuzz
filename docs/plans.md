# Short-term plan

## Slice: compiler-emitted reference counting

Core value types come before mobile packaging (see [`gaps.md`](gaps.md)). `Float` is in. `Map` / `Set` wait on shared structure, so the compiler must emit retains/releases next.

- Emit retain/release on owned values (strings, lists, ADTs, IO handles) at copies and last use. Immutable data forms no cycles, so do not add a cycle collector.
- Keep libc `malloc`/`free` through `sz_alloc` / `sz_free`. Panic may still leak.
- Proof: `examples/kernel` and `examples/io` still pass `scuzz check` and `scuzz test`. Alloc accounting on a counter-shaped Headless pump stays flat across extra pumps.
- Out of scope: `Map` / `Set`, cycle collection, OS threads.
