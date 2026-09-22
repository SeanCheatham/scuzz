# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings. Kit calls compare pre-parsed `Ty` values.

## Return Ty from env lookup

`getTy` shows the env type. Callers parse the string again. Return `Ty` from env lookup.

Proof: `scuzz check examples/compiler` stays green. `scuzz fuzz --iterations 0 examples/counter` stays green.
