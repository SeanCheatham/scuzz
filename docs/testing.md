# Testing and empirical optimization strategy

How Scuzz verifies programs today (laws + `scuzz fuzz`), and the direction that story grows into: **schedule search (DST)** for concurrency correctness, then **empirical pre-optimization** — machine-specific tuning discovered by search and verified by the same fuzzer. Mechanics of laws, sim overlays, and the fuzz CLI live in [`vision.md`](vision.md); this doc owns the strategy and its staging.

## Why Scuzz can do this

Separating *performance* from *meaning* only works if the language pins meaning down tightly enough that execution strategy can vary without changing observable behavior. Scuzz's existing design locks provide exactly that:

- **Closed impurity.** All nondeterminism and external I/O go through blessed `IO`; no app-level escape hatch. Observational equivalence is well-defined: same signal store, same a11y dump, same law results.
- **Deterministic fuzz contract.** `(program + sim, seed/config, event script) → exit code + signal store + a11y dump + law results` is a pure function of its inputs.
- **Laws as the oracle.** Correctness is declared properties over the signal store + a11y dump — not timing, not interleaving order, not pixels.

The payoff: **the fuzzer doubles as an equivalence checker.** Any transformation that claims to preserve meaning (a different fiber schedule, a parallel execution strategy, a tuned build) is validated by replaying the same corpus and asserting identical observable outputs plus no law violations. Classic autotuners (Halide schedules, PGO, BOLT) must *assume* their transformations are safe; Scuzz search-verifies them against the fixed observation surface.

## Layer 1 — laws + fuzz (exists)

The current story, owned by `vision.md`: authors declare laws, `scuzz fuzz` searches typed event scripts (seeded random or `--exhaust`), failures replay deterministically from `repro.toml`. The search space is **inputs**: taps, text, pumps.

## Layer 2 — schedule search / DST (direction)

Deterministic simulation testing extends the search space from inputs to **fiber interleavings**. Today the scheduler is a fixed policy (FIFO ready queue, left-before-right fork), so every fuzz run explores one schedule. The slice:

- Make scheduler choices **seed-driven under fuzz**: at each scheduling point, the ready-fiber pick comes from the run's seeded generator instead of the fixed policy. Live runs keep the fixed policy.
- `repro.toml` records the schedule seed alongside the event script; replay is exact.
- Oracles are unchanged — laws, panic/`SzError` exit.

This is a prerequisite for everything in Layer 3: auto-parallelization is only safe if adversarial schedules can be searched for law violations first. It also forces the scheduler seam (pluggable pick policy) that a parallel runtime needs anyway.

## Layer 3 — empirical pre-optimization (direction, post-parallelism)

Once execution strategies exist that can differ (OS threads for IO are a deferred residual in [`gaps.md`](gaps.md)), performance becomes a searchable space with fuzz as the safety net. Think offline PGO/autotuning rather than JIT: measure ahead of time on the target machine, ship a static tuned build.

### The artifact: `*.scuzz_tune`

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
- **Fuzz scripts are the wrong workload.** They are adversarial/random, not representative. Tuning measures against a separate **bench corpus** — authored or recorded representative sessions reusing the same script line protocol (`tap` / `text` / `pump`).

Pipeline: search generates candidate tunings → measure on bench corpus → fuzz-verify equivalence → emit `*.scuzz_tune` → tuned build applies it.

## Staging and proof bars

| Stage | Slice | Proof |
| --- | --- | --- |
| 1 | Seed-driven fiber scheduling under `scuzz fuzz`; schedule seed in `repro.toml` | A seeded schedule finds (and deterministically replays) an interleaving bug a fixed-policy run cannot |
| 2 | One hand-written knob on one construct + `*.scuzz_tune` applied at build; fuzz-equivalence gate | Tuned and default builds are fuzz-equivalent; the knob measurably changes a bench metric |
| 3 | Search loop over stage 2 (auto-tuning) | Generated tune file beats the default build on the bench corpus and passes the gate |

Stage 2+ is blocked on a parallel execution strategy existing (OS threads residual). Do not build the tuner before there is something to tune.

## Risks

| Risk | Mitigation |
| --- | --- |
| Fuzz verification is probabilistic, not proof | Conservative knob vocabulary — each knob individually argued semantics-preserving; fuzz is belt-and-braces, not the sole argument |
| Ordering-sensitive observations (e.g. interleaved `Queue` consumption) falsify "equivalent under any schedule" | Tuner only parallelizes regions whose observations are order-independent; dump comparison catches violations |
| Tune files become load-bearing config | Non-load-bearing rule above; default build stays correct; regenerate, never hand-edit |
| Scaffolding before a vertical slice | Staging order above; Layer 2 lands alone and pays for itself as a correctness tool |
