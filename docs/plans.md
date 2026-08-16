# Short-term plan

## Slice: Last-use release for let / `for` binders

The compiler releases owned string, List, Map / Set, and ADT temps after last use. Binders (`let` / `for`) drop the owned flag, so bound values stay allocated.

- Track owned on locals. Release the binder after last use in the body.
- Proof: compiler IR or runtime alloc accounting shows a bound temp returns toward baseline.
- Out of scope: cycle collector, OS threads, device packaging.
