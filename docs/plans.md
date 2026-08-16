# Short-term plan

## Slice: Last-use release for List temps

The compiler releases owned string temps after concat, slice, trim, and `IO.println`. List cells retain heads and tails. List temps without a last-use stay allocated.

- Emit retain/release on owned List temps after last use (same shape as strings).
- Proof: compiler IR or runtime alloc accounting shows a dropped List temp returns toward baseline.
- Out of scope: cycle collector, OS threads, device packaging.
