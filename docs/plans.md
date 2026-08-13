# Short-term plan

## Next

**Verification pivot, slice 3: `*.scuzz_drivers`.** Impure parameterized overlay defs, verify-graph only, `Law.*` rejected inside; verify build publishes the driver table; fuzz alphabet + script protocol gain `drive <name> [args]`. Then slice 4 `where` refinements: [`gaps.md`](gaps.md).

**Then.** `IO.timeout(ms)` — blessed race of sleep-fail vs inner; cancel already runs ensure/Resource finalizers. Then language `Fiber`, then `forever` / `repeatN` / `retryN`.
