# Next slice

Compile-time performance. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Parse `Param` and `Fun` stay strings.

## Coverage from the live program

`Verify.coverageBoth` parses compiled files when they differ from live. Use the live `Prog` when the file set is the same.

Proof: `scuzz fuzz --iterations 0 examples/counter` stays green. `scuzz check examples/compiler` stays green.
