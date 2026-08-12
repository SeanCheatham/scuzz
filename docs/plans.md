# Short-term plan: self-host thin generics

**Goal.** Mirror Stage-0 monomorphized `def id[T](…)` in `compiler-scuzz/` (parse + typ + format); smoke `examples/generic` in `scripts/selfhost.sh`. Do not rewrite the compiler onto generics.

## Status

Stage 0 + `examples/generic` done. Self-host next.

## Non-goals

Multiple type params (unless free), trait bounds, `List[T]` / ADT type params, implicits, HKT.
