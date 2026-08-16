# Short-term plan

## Slice: Last-use release for ADT temps

The compiler releases owned string, List, and Map / Set temps after last use. ADT construct temps without a last-use stay allocated.

- Emit retain/release on owned ADT temps after last use (same shape as List).
- Proof: compiler IR or runtime alloc accounting shows a dropped ADT temp returns toward baseline.
- Out of scope: cycle collector, OS threads, device packaging.
