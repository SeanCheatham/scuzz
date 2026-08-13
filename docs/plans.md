# Short-term plan

## Next

**Verification pivot, slice 2: `Law.check` + `Law.sometimes`.** Pure `Law.check(name, ok, value): T` (identity live, residual under verify) so invariants live in pure code; `Law.sometimes(name)` accumulates per run, campaign aggregation in the fuzz CLI. Then slice 3 `*.scuzz_drivers`, slice 4 `where` refinements: [`gaps.md`](gaps.md).

**Then.** `IO.timeout(ms)` — blessed race of sleep-fail vs inner; cancel already runs ensure/Resource finalizers. Then language `Fiber`, then `forever` / `repeatN` / `retryN`.
