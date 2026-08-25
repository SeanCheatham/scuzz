# Next slice

## Mining qualification

Make candidate quality measurable. A candidate qualifies on evidence, not on observed frequency. Design: [`mine.md`](mine.md). ML posture: no ML inside `scuzz`; advisory triage runs outside through `--json`.

Steps in order:

1. **Evidence upgrade.** Invariant candidates print minimum, maximum, run count, and attainment (runs that reach the minimum; whether an observed decreasing event fired at the minimum). Skip a candidate no event can fail. This closes the `mine.md` evidence gap.
2. **Mutation-kill rank.** Replay each candidate against the surviving mutants from the last campaign summary. Evidence gains `kills K/N survivors`. Kills become the first rank key, then kind, then the current keys.
3. **Directed falsification.** Probe each invariant candidate with a short seeded campaign that weights events observed to decrease the signal. A violated candidate does not list. Evidence gains the probe count. The seed is fixed and deterministic.
4. **`scuzz mine --json`.** Emit the candidate list as JSON: id, kind, sentence, evidence fields. The decision flags stay the only write path. An external assistant (for example a local LLM) consumes the JSON and drives `--always` / `--never` / `--approve` / `--dismiss`. `scuzz` embeds no inference runtime.
5. **Dismissal provenance.** `--dismiss` writes `# dismissed: <id> <sentence>`. The miner suppresses by id or by exact sentence, so dismissals survive hash churn.
6. **Docs and CI.** Update the `guide.md` mine paragraph. The CI counter loop asserts the new evidence fields and exercises `--json`.

Verification:

- `scuzz mine examples/counter` lists min/max/attainment evidence and `kills K/N` on each candidate.
- A candidate that kills no survivor ranks below one that kills any.
- Negative: introduce a decrement bug in the counter handler. The directed probe violates the stale range candidate and it does not list.
- `--json` output round-trips: a script parses it and drives `--dismiss` by id.
- `make -C crates/runtime test`, `cargo test -p scuzz-compiler`, and `cargo test -p scuzz` pass.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
