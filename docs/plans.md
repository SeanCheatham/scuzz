# Next slice

Named Timeline observations and temporal helpers.

Claims in `examples/counter`, `examples/studio`, `examples/editor`, and `examples/bad-response` read integer dump slots (`Timeline.signalInt(t, i, 0)`). They copy `alwaysHas` / `afterHitShows`. The author surface must name Signals. Two `Verdict` helpers must cover those folds. Locks: [`vision.md`](vision.md#signal-string-and-errors). Gap 1: [`gaps.md`](gaps.md).

## Done when

- A Signal publishes its binder name (`count = Signal.int(0)` → `"count"`).
- `Timeline.signalInt` / `signalListLen` / `signalStrHas` and `Property.signalInt` / `signalStr` / `signalListLen` / `signalListAt` take that name. Integer ids leave the author kit.
- `Verdict.alwaysHas(t, needle)` and `Verdict.afterHit(t, hit, needle)` are kits. Verify files stop copying those folds.
- `examples/counter`, `examples/studio`, `examples/editor`, `examples/bad-response`, and `examples/shared` use names and the helpers.
- `scuzz check` and `scuzz fuzz --iterations 0` pass on those packages except `examples/bad-response`, which still fails from `corpus/`.
- Dump files may still print `int[0]` as an implementation detail. Authors do not write `0`.

## Out of slice

`Signal[T]`, UTF-8 `String`, LSP spans, typed `E`, agent JSON schema, `tap N` rename, HTTP, `Gen[T]`, source-region coverage.
