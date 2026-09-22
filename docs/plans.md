# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings. Kit calls compare pre-parsed `Ty` values.

## Keep checkKnownRet as Ty

`checkKnownRet` still calls `ok(kitRetFromTy(...))`, which shows the inferred type and parses the return string. Keep `Ty` for the common return path. Special kit returns may still use strings.

Proof: `scuzz check examples/compiler` stays green. `scuzz fuzz --iterations 0 examples/counter` stays green.
