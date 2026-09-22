# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings. Known kit calls compare pre-parsed `Ty` values.

## Generic kit wants as Ty

`resolveGenericKitSig` still passes string params into `genericArgs`, which parses each want at the call. Use `KitSig.params` and compare `Ty` values.

Proof: `scuzz check examples/compiler` stays green. `scuzz fuzz --iterations 0 examples/counter` stays green.
