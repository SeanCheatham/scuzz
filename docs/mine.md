# Mining and the judgment queue

Status: shipped today, `scuzz mine` lists invariant, response, and boundary candidates from `build/fuzz/trace.campaign`, with evidence, `--json`, decision flags (`--always` / `--never` / `--approve` / `--dismiss`), and dismissal suppression. This doc describes the target design: claims are Scuzz predicates over timelines, candidate synthesis runs inside the campaign, and `scuzz mine` is the judgment-queue interface. Sections mark what is shipped.

## The model

A **claim** is a pure Scuzz predicate over a `Timeline`. A timeline is the recorded linear history of one execution: one branch of the multiverse the campaign explores. Claims quantify over the multiverse.

- **∀ claims** must hold on every explored timeline. Invariants, response claims, and `never` claims are ∀.
- **∃ claims** must hold on at least one explored timeline. `Property.sometimes` is ∃.

Most claims pair a trigger with an expectation. The trigger names the states a claim speaks about. The expectation names what those states must contain. An invariant is the degenerate case: the trigger is always true. A metamorphic relation is the one extension: it relates two timelines (a permuted input keeps the total). It stays a distinct claim kind.

Claims judge complete timelines at the terminal point. `scuzz fuzz` constructs that point. When the event budget runs out, the run stops injecting events, injects shutdown where it applies, and pumps until fibers settle or a quiesce budget trips. A pending expectation at a genuine end fails. A truncated run is not possible: quiesce comes first.

`.require` stays a live point assertion. It aborts a run the moment a wrong value exists. Timeline claims judge after the run. Two strata: point assertions abort. Timeline claims judge.

## State

Each pump records a `State`. A timeline is a list of states. `State` has three sections.

- **Observation.** Signal store, a11y dump, `[last_hit]`, fields and editor state.
- **Effect.** The blessed-op log since the last pump (op name, sizes or hashes of args, never payloads), the fiber census (statuses, park reasons, queue depths), heap deltas, and the fault context.
- **Drive.** Drive args and results.

The capture is deterministic. Creation-order ids only. No pointer addresses. No hash-map iteration order. `State` is a versioned schema. A field enters when a second claimant needs it. Claims read only `State`. A claim can judge only what `State` exposes.

## The campaign

`scuzz fuzz` explores the multiverse one timeline at a time, in memory.

1. Replay `<pkg>/corpus/*.toml`, then `build/seeds.txt` Given rows. Then search.
2. Each run executes under TestRuntime, reaches its quiesced terminal point, and evaluates every armed claim against the timeline in memory.
3. Verdicts fold into campaign aggregates. A ∀ claim fails the campaign on one counterexample timeline. An ∃ claim fails the campaign when no timeline satisfies it.
4. Persist identities and aggregates. Never persist full timelines. A timeline's identity is its seed, event script, and fault plan. Determinism re-derives the recording by replay.

The corpus stores identities. `repro.toml` stores an identity. `summary.toml` stores aggregates. The judgment queue stores one slice per queued item, or the identity when replay is cheaper.

## Candidate synthesis

Synthesis is a streaming fold over the same timelines the claims judge. Candidates can never be stale. They come from the campaign that just ran. The merged full-trace artifact (`trace.campaign`, shipped) goes away.

Four sources:

1. **Holds.** Classic mining (shipped). Fold per-signal minima, post-tap row frequencies, and boundary states across all timelines. Emit the candidate as a claim.
2. **Forks.** Undecided states. Two timelines diverge from a similar prefix, coverage reports a gap, or a reached state varies in a dimension no claim reads. Emit the state as a forced-choice question with its replay identity.
3. **Complaints.** A human marks a replayed timeline as wrong. Shrink it. Compute the weakest claim that excludes the wrong state and keeps every observed good state. Emit it as a candidate.
4. **Mutant pairs.** A mutant survives and its replayed timelines diverge from the original under the same seed. Emit the pair as a question: timeline A does X, timeline B does Y, everything else identical. The accepted answer writes a claim that kills the mutant by construction. A mutant with bit-identical replayed timelines is inert. It does not list.

No ML in generation, ranking, validation, or enforcement. Cap the queue at 20 items.

## Qualification

Knowing which item deserves the human's time is the hard job. Make it measurable. An item qualifies on deterministic measures, not on observed frequency. (Shipped for holds candidates.)

- **Boundary evidence.** Invariant evidence prints minimum, maximum, run count, and attainment: how many runs reach the minimum, and whether an observed decreasing event fired at the minimum. A minimum reached under pressure is a strong candidate. A minimum the campaign never approached is noise. Skip a candidate no event can fail.
- **Mutation-kill rank.** Replay each candidate against the surviving mutants of the last campaign. Evidence gains `kills K/N survivors`. Kills become the first rank key. A claim that kills no survivor carries no enforcement weight and ranks last.
- **Directed falsification.** Before a candidate lists, probe it with a short seeded campaign that weights events observed to decrease the signal. A violated candidate does not list. The seed is fixed, so the probe is deterministic on every machine.
- **Mutation relevance.** Rank fork and coverage items by adjacency to mutation sites. An unclaimed dimension that mutants perturb is a real hole. One that mutants never touch is likely incidental.

## The judgment queue

`scuzz mine` is the queue interface. (Shipped as the candidate list.)

- `scuzz mine <pkg>` lists queue items. One line each: id, kind, rendered English, evidence. The id is the first 16 hex of sha1(`kind|rendering`), same style as corpus hashes.
- A human judgment is one forced choice per item: accept, reject, or defer. Each item carries its replay identity and its consequence in one glance: what accepting forbids in all future campaigns. Reject and defer never grant authority. Silence never accepts.
- An agent accepts only a candidate it can cite. `--always` / `--never` take `--because "<line>"`. The flag names a product-doc intent line or a judgment-log entry that entails the claim. The cited line must already exist. The CLI fails `--because` when it is absent. Intent comes first. Observation comes second. This is preregistration.
- Validation stays shipped behavior: replay the corpus plus 8 fresh seeds with the claim armed. A failure prints the counterexample repro and leaves the file unchanged. Validation filters false claims, not incidental ones. The human judges intent.
- `--approve` replays a boundary script and writes `corpus/<hash>.toml` on pass. (Shipped.)
- `--dismiss` writes a suppression entry with the rendered sentence. A dismissed candidate never shows again. (Shipped.) The suppression stays visible in review.
- `--json` emits the queue for an external assistant. The assistant may reorder, annotate, or dismiss. It accepts only through the validated decision flags, with `--because`. `scuzz` embeds no inference runtime. CI needs no GPU. Semantic (embedding) novelty stays out of scope. It breaks campaign reproducibility.

## Claims as Scuzz source

Claims live in `intent.scuzz_intent` as Scuzz predicates over the claim API. The claim API reads `State`: signal store, a11y rows, hit history, effect log, fiber census, drive records. A claim is a single pure expression. No helper defs. A claim that needs helpers belongs in app code or in `.require`.

The closed English grammar stays in the compiler as the **specification of the renderable fragment**. The renderer states each claim in English for the judgment queue and for diffs. The check is a round trip: parse the rendered English and require the original claim AST. A mismatch is a renderer bug and fails the build. If a claim cannot render into a faithful one-glance question, it does not belong in the claims file.

The claims file is tool-maintained Scuzz source. This is the `scuzz fmt` class of tool writes, not `*.g.scuzz` codegen. The codegen lock stands. Humans and agents may also hand-write claims. Hand-written claims need no citation.

## The judgment log

The judgment log is the provenance root. It is structured, append-only, and machine-written. Each entry records the question, the options, the replay identity, the choice, and the resulting claim line. The intent file is a projection of the judgment log. A hand-written claim records as a judgment without a question. Human prose intent lives in product docs. The log cites it.

## Coverage

Three axes measure the oracle net. Each axis feeds the queue.

- **Reachability.** `sometimes` names and claim triggers must fire in the campaign. An unfired trigger fails the campaign.
- **Breadth.** A claim covers the `State` fields it reads. `scuzz check` reports defs, signals, and controls with no claim, no `sometimes`, and no `.require`. The campaign reports reached states that vary in fields no claim reads.
- **Strength.** Mutation. A divergent survivor in a claimed field means a weak claim. In an unclaimed field it means a missing claim. Both feed the queue.

## Proof

Shipped proofs stay: the counter campaign writes `build/signals.txt`; `mine` lists a `stays` candidate with min/max/kills evidence; `--json` emits an array; `--dismiss` suppresses; `--always` validates and appends; the decrement-probe negative drops a stale candidate.

Direction proofs:

- `scuzz fuzz --iterations 16 examples/counter` ends each run in a quiesce phase. Claims evaluate at terminal points in memory. No `trace.campaign` is written.
- A rendered claim round-trips through the English grammar to the same AST. A patched renderer fails the build.
- A seeded mutant with divergent replayed timelines lists as a minimal-pair queue item. Accepting it writes a claim that kills the mutant.
- A campaign that reaches a state varying only in unclaimed fields lists a breadth item. Dismissing it suppresses that field across campaigns.

## Contingencies

- **Aggregates lose per-state detail.** Mining works from folds, not recordings. When a queue item needs a slice, replay the identity. Decide with data which slices to persist.
- **Signal table drift.** If `build/signals.txt` ids ever disagree with claim evaluation, `stays` claims break first. The existing `binds_signal_from_main` test catches the order change. Fix the table, not the claim.
- **Hash churn.** Candidate ids change when the rendering changes. Sentence-bearing suppression entries keep suppression across id churn.
- **Queue ergonomics.** Every item must carry its replay and its consequence in one glance. An item that needs three files of context gets dismissed unread. Gate new question kinds on this.
- **Equivalent mutants.** The bit-identical replay filter demotes inert mutants on evidence. It is not a proof of equivalence. Expect residue. Rank it last.
