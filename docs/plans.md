# Short-term plan

## Next

**Verification pivot, slice 1: in-source `law` declarations.** `law name: Bool = …` at top level in live `*.scuzz`; collect at parse, residualize exactly like today's overlay laws, erase from live builds. Delete `*.scuzz_laws` in the same change (overlay kind in Stage 0 + `compiler-scuzz/`, example migrations, guide). Slices 2–4 (`Law.check` / `Law.sometimes`, `*.scuzz_drivers`, `where` refinements): [`gaps.md`](gaps.md).

**Then.** `IO.timeout(ms)` — blessed race of sleep-fail vs inner; cancel already runs ensure/Resource finalizers. Then language `Fiber`, then `forever` / `repeatN` / `retryN`.
