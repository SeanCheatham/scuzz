# Evaluator slice 5: branching and coverage

Locks: [`philosophy.md`](philosophy.md#evaluator). Arc: [`vision.md`](vision.md#evaluator-arc). Steps run in order. Each step ends with a proof and a commit.

## Step A: probe server

Status: done.

Per-probe cost is the process spawn plus parse and check of the package. `examples/api-report` spends 0.19 s per idle probe on the evaluator and runs about 2700 probes per campaign, so the evaluator campaign is slower than the compiled one.

- `scuzz eval --probe DIR` loads the package once and serves probes: each `probe` line on stdin runs one probe and prints its exit code on stdout. EOF ends the server.
- The runtime forks each probe when `SCUZZ_EV_REQUEST` names a request file. The child applies `KEY=VALUE` lines from that file as `SCUZZ_KEY`, sends stdout and stderr to `SCUZZ_PROBE_LOG`, sets the 512 MiB address limit on Linux, runs the probe, and exits. The parent waits under the 20-second deadline, kills a late child, clears the probe registrations, and reports the exit code.
- `Drive` keeps one server per prepared file set: the search set under `build/fuzz/ev`, and one per mutant. `kv` owns shell quoting so the same env builders write the request file.
- Proof: `scripts/ci-fuzz.sh` diffs both engines on `examples/webhook` and `examples/api-report` and the evaluator campaign is not slower than the compiled one.

## Step B: scheduler step parity

Status: done.

A scheduler step is one effect. `pure`, `flatMap`, `handleError`, `attempt`, `ensure`, and loop entry spin in the same step as the effect that follows, so the evaluator's extra `IO` wrapping does not move the interleaving. Proof: `scripts/ci-fuzz.sh` runs `examples/io` on both engines and diffs the summaries.

## Step C: evaluator speed on large packages

Status: done.

The idle probe on `examples/kernel` went from 21 s to 12 s and on `examples/fmt` from 21 s to 18 s. Cuts: one location string per def computed at load instead of per call, a plain-argument fast path around `Check.alignCall`, one Ftab lookup per call, literal patterns before the pattern scans, constructor fields cached per enum case, and character checks instead of `Str.slice` in the pattern scans. Proof: both idle probes run under the 20-second deadline and `scripts/ci-fuzz.sh` runs `examples/kernel` on the evaluator.

## Step D: search feedback

Status: pending.

Snapshot and fork at scheduler steps. Comparison operand distance from the evaluator feeds search. Proof: a `Property.sometimes` that compiled search does not reach in budget and evaluator search does.
