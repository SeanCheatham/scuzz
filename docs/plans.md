# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings. Kit calls compare pre-parsed `Ty` values.

## Check concreteTy on Ty

`concreteTy` parses a shown type. Callers pass `tyStr` of an `Out` that already holds `Ty`. Check that `Ty`. Annotation strings may still parse.

Proof: `scuzz check examples/compiler` stays green. `scuzz fuzz --iterations 0 examples/counter` stays green.
