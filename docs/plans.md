# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings. Kit calls compare pre-parsed `Ty` values.

## Keep zipCheck acc as Ty

`zipCheck` stores the accumulated type as a `String`. `ok(acc)` parses it on every known call. Thread `Ty` through `zipCheck`.

Proof: `scuzz check examples/compiler` stays green. `scuzz fuzz --iterations 0 examples/counter` stays green.
