# Empirical pre-optimization (future)

Not a current priority. Laws, sim overlays, and `scuzz fuzz` (including schedule search) live in [`vision.md`](vision.md). This doc is the **later** bet: separate performance from meaning — machine-specific execution strategy discovered by search and verified by the same fuzzer. Do not start this work until a parallel execution strategy exists (OS threads residual in [`gaps.md`](gaps.md)).

## Why this can wait — and why it can work

Separating *performance* from *meaning* only works if the language pins meaning down tightly enough that execution strategy can vary without changing observable behavior. Existing locks already do that:

- **Closed impurity.** All nondeterminism and external I/O go through blessed `IO`. Observational equivalence is well-defined: same signal store, same a11y dump, same law results.
- **Deterministic fuzz contract.** `(program + sim, seed/config, event script, schedule seed) → exit code + signal store + a11y dump + law results` is a pure function of its inputs.
- **Laws as the oracle.** Correctness is properties over the signal store + a11y dump — not timing, not interleaving order, not pixels.

The payoff when we get there: **the fuzzer doubles as an equivalence checker.** Any transformation that claims to preserve meaning (a parallel execution strategy, a tuned build) is validated by replaying the same corpus and asserting identical observable outputs plus no law violations. Classic autotuners (Halide schedules, PGO, BOLT) must *assume* their transformations are safe; Scuzz can search-verify them against the fixed observation surface.

Schedule search under fuzz is already the correctness half of that story (`vision.md`). Optimization is the performance half — blocked on having more than one execution strategy to choose.

## Direction: `*.scuzz_tune`

Once strategies can differ, performance becomes a searchable space with fuzz as the safety net. Think offline PGO/autotuning rather than JIT: measure ahead of time on the target machine, ship a static tuned build.

A stem-paired sidecar, following the existing overlay convention:

```text
src/
  Todo.scuzz          # meaning
  Todo.scuzz_sim      # test-time substitution
  Todo.scuzz_laws     # correctness spec
  Todo.scuzz_tune     # machine-specific execution strategy
```

Design constraints:

- **Semantics-preserving by construction.** The tune vocabulary can only express strategy knobs — fork fan-out, fiber-to-thread mapping, fusion/inlining/memoization decisions, reconciliation batch sizes. It is *incapable* of expressing a meaning change, the same way sim overlays are restricted to same-name/same-type/same-purity.
- **Non-load-bearing.** Deleting the file yields the correct default build. Tune files are per-target-machine output artifacts, not source of truth; they are regenerated, never migrated.
- **Fuzz-gated.** A tuned build must replay the fuzz corpus (including schedule seeds) with observable outputs identical to the default build and no law violations before it is accepted.

### Cost signal and bench corpus

Fuzz answers "does anything break?"; tuning answers "which strategy is fastest?" — a different oracle with two consequences:

- **Virtual time measures nothing.** TestRuntime jumps to the next wakeup, so tuned and untuned programs look identical under it. Cost needs deterministic proxies (instruction counts, allocation accounting) or wall-clock measurement on the target machine. Wall-clock is what makes the manifest *machine*-specific.
- **Fuzz scripts are the wrong workload.** They are adversarial/random, not representative. Tuning measures against a separate **bench corpus** — authored or recorded representative sessions reusing the same script line protocol (`tap` / `text` / `pump` / `scroll`).

Pipeline: search generates candidate tunings → measure on bench corpus → fuzz-verify equivalence → emit `*.scuzz_tune` → tuned build applies it.

## Staging (when this is in scope)

| Stage | Slice | Proof |
| --- | --- | --- |
| 1 | One hand-written knob on one construct + `*.scuzz_tune` applied at build; fuzz-equivalence gate | Tuned and default builds are fuzz-equivalent; the knob measurably changes a bench metric |
| 2 | Search loop over stage 1 (auto-tuning) | Generated tune file beats the default build on the bench corpus and passes the gate |

Do not build the tuner before there is something to tune.

## Risks

| Risk | Mitigation |
| --- | --- |
| Fuzz verification is probabilistic, not proof | Conservative knob vocabulary — each knob individually argued semantics-preserving; fuzz is belt-and-braces, not the sole argument |
| Ordering-sensitive observations (e.g. interleaved `Queue` consumption) falsify "equivalent under any schedule" | Tuner only parallelizes regions whose observations are order-independent; dump comparison catches violations |
| Tune files become load-bearing config | Non-load-bearing rule above; default build stays correct; regenerate, never hand-edit |
| Scaffolding before a vertical slice | Staging order above; first slice is one knob that pays for itself |
