# Plan: evaluator slice 4 (fuzz engine)

Arc and slice order: [`vision.md`](vision.md#evaluator-arc). Locks: [`philosophy.md`](philosophy.md#evaluator). Delete this file when the slice is done.

## Goal

`scuzz fuzz` runs search probes and mutant probes on the evaluator. Corpus replay, `--relate`, and `--replay` stay compiled. A UI package (`[ui]`) keeps compiled probes until the browser slice lands `View`, `Signal`, and `Ui` cases.

## Constraint

Toolchain source (`examples/compiler`, `examples/cli`, `examples/shared`) can only call kits that the newest GitHub `v*` release compiles. The slice lands in two steps with a release between them.

## Step 1: in the tree

- `Value` has `VTimeline` and `VVerdict`. `Property.*`, `Timeline.*`, and `Verdict.*` map to native kits. `Property.check` applies a lambda or forces an `IO[Bool]` predicate. `Scenario.context` reads `EvProg.ctx`, a `Ref` the probe entry fills in step 2; outside a probe it fails loud. `Eval.excludedKits()` keeps the UI prefixes, the live signal readers, and `Fuzz.`.
- Runtime hooks in `crates/runtime/src/testrt.c`: `sz_fuzz_setup`, `sz_fuzz_driver`, `sz_fuzz_verify`, `sz_fuzz_verify_rel`, `sz_fuzz_hit`, `sz_fuzz_probe`. Closure drivers get the drive line as `List[String]`. Closure claims get a `Timeline` or a `(Timeline, Timeline)` pair. The probe copies `SCUZZ_EV_*` to `SCUZZ_*`, refreshes cached env reads, installs TestRuntime under `SCUZZ_TESTRT=1`, runs setup, runs the drive script or the program, ends the session, and flushes the dumps. Test: `test_fuzz_probe_closures` in `crates/runtime/tests/test_io.c`.
- `Kits.scuzz` rows `Fuzz.setup`, `Fuzz.driver`, `Fuzz.verify`, `Fuzz.verifyRel`, `Fuzz.hit`, `Fuzz.probe`. `Emit` lowers them. Proof: `examples/codegen` prints `probe-ok` under the env `scripts/ci.sh codegen` sets.

Cut a release before step 2.

## Step 2: after the release

1. **Probe entry.** `Eval.probe(files): IO[Unit]` loads the prepared fuzz files (the list `Drive.fuzzCollect` emits: scenario-applied sources, `rewriteReqFiles`, and the `__verify` wrapper). Register `setup` through `Fuzz.setup` and store its value in `EvProg.ctx`. Register every `_drv_*` def through `Fuzz.driver` with a closure that converts tokens by parameter type (`Int`: `Str.toInt(tok, 0)`; `Bool`: `true` or `1`; `String`: the token) and runs the evaluated IO. Register every `Timeline => Verdict` def through `Fuzz.verify` and every two-`Timeline` def through `Fuzz.verifyRel`. Call `Fuzz.hit(Check.covKeyOf(...))` at def entry and at `if` and `match` arms with the keys `Emit.covHitIf` interns. Run `Fuzz.probe(main)`.
2. **CLI.** `scuzz eval --probe DIR` reads `DIR/*.scuzz` as prepared files, runs `check`, then `Eval.probe`. A check failure exits nonzero with the check message (a mutant that does not compile).
3. **Drive.** `fuzzSearchRun`, `fuzzShrinkTry`, and mutant probes spawn `scuzz eval --probe` with the `fuzzProbeEnv` values under the `SCUZZ_EV_` prefix when `!job.hasUi`. Mutants write `Mutate.applyKept` files to `build/fuzz/mutate/<site>/ev/` instead of emit and link. A promoted search failure replays compiled before `fuzzDone`; a difference fails the campaign. Corpus replay, `--replay`, `--relate`, live paint, and the split check stay compiled.
4. **Proof.** `scripts/ci-fuzz.sh` runs `examples/webhook` and `examples/api-report` and compares `build/fuzz/summary.json` against a compiled-probe run (`SCUZZ_FUZZ_ENGINE=compiled`) on `fuzz.search`, `fuzz.search_failures`, `mutate.*`, `coverage`, `sometimes`, and `triggers`. Wall clock prints for both. `examples/counter` stays compiled and is the UI control.
