# Short-term plan

## Next

**Verification pivot, slice 4: `where` refinements.** On `def` params and `record` fields; checker synthesizes residual checks at call/construction; erased live; no SMT, no refined-type subtyping: [`gaps.md`](gaps.md).

**Then.** `IO.timeout(ms)` — blessed race of sleep-fail vs inner; cancel already runs ensure/Resource finalizers. Then language `Fiber`, then `forever` / `repeatN` / `retryN`.
