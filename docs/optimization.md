# Empirical pre-optimization (future)

Not a current priority. Properties, sim overlays, and `scuzz fuzz` (including schedule search) live in [`vision.md`](vision.md). This doc is later work: separate performance from meaning. Search finds a machine-specific execution strategy. The same fuzzer verifies it. Do not start this work until a parallel execution strategy exists (OS threads residual in [`gaps.md`](gaps.md)).

## Why this can wait — and why it can work

Separating *performance* from *meaning* works only if the language pins meaning tightly. Then execution strategy can vary without a change in observable behavior. Existing locks already do that:

- **Closed impurity.** All nondeterminism and external I/O go through blessed `IO`. Observational equivalence is well-defined: same signal store, same a11y dump, same property results.
- **Deterministic fuzz contract.** `(program + sim, seed/config, event script, schedule seed) → exit code + signal store + a11y dump + property results` is a pure function of its inputs.
- **Properties as the oracle.** Correctness is properties over the signal store + a11y dump. Not timing. Not interleaving order. Not pixels.

When this is in scope, the fuzzer also checks equivalence. Any transform that claims to keep meaning (a parallel execution strategy, a tuned build) must replay the same corpus. Observable outputs must match. Properties must not fail. Classic autotuners (Halide schedules, PGO, BOLT) assume their transforms are safe. Scuzz search-verifies them against the fixed observation surface.

Schedule search under fuzz is already the correctness half (`vision.md`). Optimization is the performance half. It is blocked until more than one execution strategy exists.

## Direction: `*.scuzz_tune`

Once strategies can differ, performance is a searchable space. Fuzz is the safety net. Think offline PGO/autotuning, not JIT. Measure ahead of time on the target machine. Ship a static tuned build.

A stem-paired sidecar, following the existing overlay convention:

```text
src/
  Todo.scuzz          # meaning (defs + properties)
  Todo.scuzz_sim      # test-time substitution
  Todo.scuzz_tune     # machine-specific execution strategy
```

Design constraints:

- **Semantics-preserving by construction.** The tune vocabulary expresses strategy knobs only: fork fan-out, fiber-to-thread mapping, fusion/inlining/memoization decisions, reconciliation batch sizes. It cannot express a meaning change. Sim overlays are limited the same way: same name, same type, same purity.
- **Non-load-bearing.** Delete the file and the default build stays correct. Tune files are per-target-machine output artifacts, not source of truth. Regenerate them. Do not migrate them.
- **Fuzz-gated.** A tuned build must replay the fuzz corpus (including schedule seeds). Observable outputs must match the default build. Properties must not fail.

### Cost signal and bench corpus

Fuzz answers "does anything break?" Tuning answers "which strategy is fastest?" That is a different oracle:

- **Virtual time measures nothing.** TestRuntime jumps to the next wakeup. Tuned and untuned programs look identical under it. Cost needs deterministic proxies (instruction counts, allocation accounting) or wall-clock measurement on the target machine. Wall-clock makes the manifest machine-specific.
- **Fuzz scripts are the wrong workload.** They are adversarial/random, not representative. Tuning measures against a separate **bench corpus**: authored or recorded representative sessions. They reuse the same script line protocol (`tap` / `text` / `type` / `key` / `caret` / `pump` / `scroll` / `backspace`).

Pipeline: search generates candidate tunings → measure on bench corpus → fuzz-verify equivalence → emit `*.scuzz_tune` → tuned build applies it.

## Staging (when this is in scope)

| Stage | Slice | Proof |
| --- | --- | --- |
| 1 | One hand-written knob on one construct + `*.scuzz_tune` applied at build; fuzz-equivalence gate | Tuned and default builds are fuzz-equivalent; the knob changes a bench metric |
| 2 | Search loop over stage 1 (auto-tuning) | Generated tune file beats the default build on the bench corpus and passes the gate |

Do not build the tuner before there is something to tune.

## Risks

| Risk | Mitigation |
| --- | --- |
| Fuzz verification is probabilistic, not proof | Conservative knob vocabulary. Argue each knob as semantics-preserving. Fuzz is extra, not the only argument |
| Ordering-sensitive observations (for example interleaved `Queue` consumption) falsify "equivalent under any schedule" | Tuner only parallelizes regions whose observations are order-independent. Dump comparison catches violations |
| Tune files become load-bearing config | Non-load-bearing rule above. Default build stays correct. Regenerate. Do not hand-edit |
| Scaffolding before a vertical slice | Staging order above. First slice is one knob that pays for itself |
