# Short-term plan

## Slice: Last-use release for Map / Set temps

The compiler releases owned string and List temps after last use. Map / Set trees share subtrees. Map / Set temps without a last-use stay allocated.

- Emit retain/release on owned Map / Set temps after last use (same shape as List).
- Proof: compiler IR or runtime alloc accounting shows a dropped Map / Set temp returns toward baseline.
- Out of scope: cycle collector, OS threads, device packaging.
