# Empirical optimization (future)

This work remains deferred until a parallel execution strategy exists.
OS thread support remains open in [`gaps.md`](gaps.md).
Current verification rules live in [`philosophy.md`](philosophy.md#verification-posture).

## Purpose and constraints

Select an execution strategy for one target machine without changing program behavior.
Measure before deployment. Ship a static tuned build.
All external IO and nondeterminism use the built-in `IO` effect.
The program, simulation, configuration, input events, and scheduler seed determine the recorded result.
Compare exit status, signals, accessibility records, and property results.
Timing, pixels, and a particular interleaving are outside this comparison.

A tuning vocabulary can change fork counts, fiber placement, fusion, inlining,
memoization, or reconciliation batch sizes. Each operation must preserve behavior.
Give a separate argument for this guarantee. Finite fuzz checks alone do not prove it.
Parallelize only regions whose observations do not depend on execution order.
Scenario replacements keep the same name, type, and purity.

## Proposed files and workflow

`*.scuzz_tune` is a proposed output artifact for one target machine.
It is not implemented.
The default build must remain correct when the file is absent.
Regenerate tune files. Do not maintain them as source or migrate them.

```text
src/Todo.scuzz           # Program and properties
Todo.scuzz_scenario     # Simulation
Todo.scuzz_tune         # Proposed execution strategy
```

Use a separate benchmark corpus of representative recorded or authored sessions.
Reuse the existing input script protocol.
Fuzz inputs exercise failures. They do not establish representative performance.
TestRuntime virtual time skips waits and cannot measure execution cost.
Use instruction counts, allocation counts, or elapsed time on the target machine.
Elapsed time measurements make the result specific to that machine.

1. Generate a candidate strategy.
2. Measure it with the benchmark corpus.
3. Replay the fuzz corpus, including its scheduler seeds.
4. Require the same recorded outputs and passing properties as the default build.
5. Write the tune file and use it for the static build.

## Implementation gates

| Order | Required behavior | Evidence |
| --- | --- | --- |
| 1 | Apply one manually selected tuning parameter to one construct | A benchmark changes. Corpus outputs match the default build. Properties pass |
| 2 | Search values for that parameter | The selected value improves the benchmark and passes the same checks |

Do not build a tuner before a useful execution strategy exists.
Keep the vocabulary small. A measured improvement does not excuse a behavior change.
