# Property mining

Status: the first slice is in. The trace channel, the signals table, and `scuzz mine` (invariant, response, and boundary candidates) are live. Next slice: qualification ([`plans.md`](plans.md)). Later slices: model-binding candidates, invariants over list signals, golden approval through the review loop.

## Goal

`scuzz fuzz` finds candidate properties in observed runs. A human judges each candidate. Accepted candidates become claims in `intent.scuzz_intent`. Approved states become corpus entries. The intent file stays the property store. The machine writes most sentences. The human edits by exception.

## Direction

The old direction grew the English grammar so humans could write more claims. This plan stops that direction. Humans judge observed behavior. Machines propose claims. The closed grammar stays small. It is the set of decisions the tool can encode and enforce.

Why the English intent mechanism stays:

- The claim forms are the encoding vocabulary. A mined decision must land in an enforceable sentence. `stays`, `never`, `After a tap`, and the `For` templates already cover the candidate kinds in this plan.
- `vision.md` locks "No `*.g.scuzz` codegen". Generated claims need a store. The intent file is that store.
- English sentences are the review surface. A human reads mined claims in a diff. A TOML or Scuzz-def store gives the same information with a worse review step.
- The mechanism is shipped, tested, and CI-proven. Deleting it buys nothing.

## Removal audit

Remove or stop:

- Stop grammar expansion. Delete the `gaps.md` line that ranks richer always/eventually templates. Mining replaces template growth.
- Stop the predicate-vocabulary idea for `For` claims (comparatives, quantifiers, `and`/`or` in prose). It was discussed. It never landed. Do not plan it again. Mined invariants take its place.
- Reframe `docs/guide.md`. Intent files become machine-maintained stores. Hand authoring stays supported. It is no longer the primary flow.

Keep (with the reason each survives):

- `After a tap` response claims and the runtime latch (`testrt.c`, `ui.c`). They are the encoding for mined trigger-then-response decisions. Mining proposes this claim kind often.
- `Property.lastHitHas` and the last-hit stash. The response trigger thunk needs them.
- `response.declared` and the CLI merge. Campaign non-vacuity matters more when a machine writes claims. A mined claim with a dead trigger must fail the campaign.
- `never` claims. They are the encoding for a rejected boundary state.
- `The <signal> stays at <n> or more.` It is the encoding for a mined range invariant.
- `For` templates, `visible` / `eventually`, `Property.sometimes`, goldens, `scuzz test --update`. Each has a live role outside mining. `scuzz test --update` may merge into the review loop later. Not this slice.

Nothing shipped is dead. The audit found no code to delete. It found plans and doc framing to delete.

## The loop

1. `scuzz fuzz` runs a campaign. Each run writes a per-pump trace. The CLI merges traces into `build/fuzz/trace.campaign`.
2. `scuzz mine <pkg>` reads the trace, `summary.toml`, and the declared artifacts. It emits ranked candidates.
3. The human picks a decision per candidate: `always`, `never`, `approve`, or `dismiss`.
4. `always` / `never` append the claim sentence to `intent.scuzz_intent` after validation. `approve` writes a corpus entry after replay. `dismiss` records a comment in the intent file. A dismissed candidate never shows again.
5. The next campaign runs with the new claims. New candidates surface at the new boundary.

## Machinery

### Trace channel

- New env `SCUZZ_TRACE_DUMP`. It names a file. Under `SCUZZ_TESTRT=1`, `sz_ui_pump_sync` appends one block per pump: a `== pump` line, the `[signals]` text from `sz_signal_dump`, the `[views]` text from `sz_view_a11y_dump`, and the `[last_hit]` line when set. Reuse the golden dump formatting in `ui.c` (~line 445). Factor it to take a `FILE *`.
- Cap the trace at 1 MiB per run. Stop appending after the cap. This stays deterministic.
- `fuzz_exec` in `crates/cli/src/cmd_fuzz.rs` (~line 1687) writes a per-run `trace.txt` path and merges it into `build/fuzz/trace.campaign` after each run. Mirror `merge_sometimes`.

### Signal name table

- Candidates name signals, not ids. The compiler writes `build/signals.txt` in the verify build: one `id<TAB>name<TAB>kind` line per top-level `name = Signal.int/str/list(...)` binding, in creation order. This mirrors the id assignment that `stays` lowering uses in `intent.rs`. Written in `driver.rs` next to `response.declared`.

### Candidate kinds (first slice)

Each candidate is encodable in the shipped grammar. No new sentence forms.

1. **Range invariant.** An Int signal's observed minimum across all trace states. Emit `The <name> stays at <min> or more.` Skip when the minimum equals a value the claim cannot fail on. Evidence: state count, run count, minimum, maximum.
2. **Response correlation.** A tap label `T` from `[last_hit]`, and an a11y row `R` present in every post-tap state of every run where `T` fired. Emit `After a tap on the "T" control, eventually the "R" control is visible.` Cap at 3 rows per tap label. Evidence: runs where `T` fired, state count.
3. **Boundary state.** The shrunk failing script of the last campaign, minus its last event. `approve` replays the shorter script and writes it as a corpus entry. `never` finds a dump row unique to that state and emits `The "<row>" control is never visible.` If no unique row exists, the candidate shows as approve-or-dismiss only.

No ML in candidate generation, validation, or enforcement. Ranking is deterministic: boundary states first, then response correlations by support, then invariants by state count. Cap at 20 candidates. The qualification slice moves mutation kills to the front of the rank.

### `scuzz mine`

New subcommand in `crates/cli` (new `cmd_mine.rs`; wire in `main.rs`).

- `scuzz mine <pkg>` lists candidates. One line each: `<id>  <kind>  <sentence>  (<evidence>)`. The id is the first 16 hex of sha1(`kind|sentence`), same style as corpus hashes.
- `scuzz mine <pkg> --always <id>` validates, then appends the sentence to the package `intent.scuzz_intent`. Validation: replay the corpus plus 8 fresh seeds with the claim armed. A failure prints the counterexample repro and leaves the file unchanged.
- `scuzz mine <pkg> --never <id>` does the same for a `never` encoding.
- `scuzz mine <pkg> --approve <id>` replays the boundary script and writes `corpus/<hash>.toml` on pass.
- `scuzz mine <pkg> --dismiss <id>` appends `# dismissed: <id>` to `intent.scuzz_intent`. The miner skips candidates whose id appears in a dismissed comment. Intent comments need no parser change.

An absent `intent.scuzz_intent` is created by the first write. Validation reuses the existing fuzz replay path in `cmd_fuzz.rs`.

## Qualification

Knowing which candidate deserves the human's time is the hard job. Make it measurable. A candidate qualifies on three deterministic measures, not on observed frequency. Slice steps: [`plans.md`](plans.md).

- **Boundary evidence.** Invariant evidence prints minimum, maximum, run count, and attainment: how many runs reach the minimum, and whether an observed decreasing event fired at the minimum. A minimum reached under pressure is a strong candidate. A minimum the campaign never approached is noise. Skip a candidate no event can fail.
- **Mutation-kill rank.** Replay each candidate against the surviving mutants of the last campaign. Evidence gains `kills K/N survivors`. Kills become the first rank key. A claim that kills no survivor carries no enforcement weight and ranks last. Mutants already replay the corpus, so this reuses the mutation phase machinery.
- **Directed falsification.** Before a candidate lists, probe it with a short seeded campaign that weights events observed to decrease the signal. A violated candidate does not list. The seed is fixed, so the probe is deterministic on every machine.

### External assistant through `--json`

Validation cannot judge whether observed behavior is intended. The human is that oracle. Semantic triage can help the human: "the count stays at 0 or more" reads as product intent; "the `text:count = 7` control is visible" reads as incidental. That judgment may come from an external assistant, for example a local LLM on the author's GPU.

- `scuzz mine --json` emits the candidate list as JSON: id, kind, sentence, evidence fields. The plain list stays the human surface.
- The assistant runs outside `scuzz` as a `--json` consumer. It may reorder, annotate, or drive `--dismiss`. It appends a claim only through the validated decision flags.
- `scuzz` embeds no inference runtime. Generation, ranking, validation, and enforcement stay deterministic and identical on every machine. CI needs no GPU.
- Semantic (embedding) dump novelty stays out of scope. It breaks campaign reproducibility.

### Dismissal provenance

`--dismiss` writes `# dismissed: <id> <sentence>`. The sentence keeps the intent file readable as a review surface and keeps suppression across id churn. The miner suppresses a candidate whose id or exact sentence appears in a dismissed comment.

## Proof

- `make -C crates/runtime test` and `cargo test -p scuzz-compiler` and `cargo test -p scuzz` pass.
- `cargo run -p scuzz -- fuzz --iterations 16 examples/counter` writes `build/fuzz/trace.campaign` and `build/signals.txt`.
- `cargo run -p scuzz -- mine examples/counter` lists a `stays` candidate for `count`.
- `cargo run -p scuzz -- mine examples/counter --always <id>` appends the claim. A following `fuzz --iterations 16` stays green.
- Negative: break the counter handler. `mine --always` on the stale range candidate fails validation and prints a repro. The intent file is unchanged.
- Dismissal persists: dismiss a candidate. A second `mine` run does not list it.
- CI runs the counter mine loop next to the fuzz block. Bounded and deterministic.

## Contingencies

- Trace size: the 1 MiB cap binds long campaigns. If mining needs more states, trace kept runs only. Decide with data.
- Sparse states: if `trace.campaign` has too few states for correlation, mine final states only in v1 and note the weaker evidence.
- Signal table drift: if `build/signals.txt` ids ever disagree with thunk ids, `stays` claims break first. The existing `binds_signal_from_main` test catches the order change. Fix the table, not the claim.
- Hash churn: candidate ids change when the grammar or a sentence changes. Sentence-bearing dismissal comments keep suppression across id churn. An id-only dismissal from before the qualification slice resurfaces once. Accept this.
