# Short-term plan

## Slice: Last-use release for bound ptrs through `if` / match

Owned `let` / `for` binders drop after an `IO`, scalar, or fresh owned-ptr body. A bound ptr that aliases through `if` or match phi stays allocated.

- Release a bound ptr after last use even when the body result is a phi of that ptr (retain the result, then drop the binder).
- Proof: compiler IR shows `if (b) xs else ys` (or match) releases the unused arm's binder.
- Out of scope: cycle collector, OS threads, device packaging.
