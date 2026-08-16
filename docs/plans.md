# Short-term plan

## Slice: Map and Set

Core value types come before mobile packaging (see [`gaps.md`](gaps.md)). `Float` is in. Reference counting covers string temps and shared list spines. `Map` / `Set` still wait on child ownership.

- Add persistent `Map[K, V]` and `Set[T]` on shared trees. List cells must own heads (typed drop) so tree nodes can share children without a cycle collector.
- Proof: `examples/kernel` constructs, looks up, and updates a map/set and still passes `scuzz check` and `scuzz test`.
- Out of scope: IO last-use, OS threads, cycle collection.
