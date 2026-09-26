# Scuzz Lang vision

Long-term planning arcs: open work and risks. Product intent, design locks, and language direction: [`philosophy.md`](philosophy.md). Keep/cut: [`compatibility.md`](compatibility.md). Ranked gaps: [`gaps.md`](gaps.md). Current slice: [`plans.md`](plans.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the next-step order changes.

## Primary arc: proposal review in the IDE

A developer reviews one proposed change at a time. The IDE evaluates the change in-memory next to the working tree. It shows both sides as timelines in two lanes. The lanes are blind: the IDE does not show which lane is the working tree. The developer keeps or rejects the change. Locks: [`philosophy.md`](philosophy.md#proposal-review).

The arc steps have landed: in-memory file-set diff on the evaluator, step view, proposal source under `build/proposals/`, the blind deck with Keep/Reject and the decision record, region focus with Randomize, and in-process hover, goto-def, and rename. Each step shipped as a vertical slice with a claim in `examples/editor/chrome.scuzz_verify` and a corpus entry.

What stays open:

1. **The generator.** How proposals are generated, and how the IDE talks to an LLM, is not decided. The deck works with any generator that writes `build/proposals/`.
2. **Keep rate.** Measure the keep rate per region before tuning the generator.

## Supporting work

Compile time limits the review loop. The IDE links the compiler, so each IDE build pays the full compiler build. Measure the commands in [`gaps.md`](gaps.md) after each compile-time change.

Compiler correctness comes before new language surface. The IDE runs the evaluator on arbitrary proposals, so evaluator and emitter differences block the arc.

Standard kits follow compile time. Close the table-stakes gaps that block ordinary programs. Ranked list: [`gaps.md`](gaps.md).

Do not start FFI, a package registry, physical-device packaging, or `*.scuzz_tune` in this work.

## Risks

| Risk | Mitigation |
| --- | --- |
| The IDE build is slow. `scuzz check examples/editor` takes 19 s cold. The IDE fuzz probe build takes 55 s to 75 s | Compile-time work in [`gaps.md`](gaps.md) applies to the IDE |
| A probe inside the IDE process corrupts IDE state or leaks | The probe forks a child, as the evaluator probe server does. The session heap oracle runs on each IDE corpus entry |
| Evaluator output differs from the emitted binary | Corpus replay runs compiled after an evaluator campaign. A difference fails the campaign |
| An evaluator probe misses the idle deadline, or its idle timeline differs from compiled | The campaign prints why and runs every probe compiled |
| Index alignment hides a real difference, or shows noise, when a proposal adds or removes a step | Show the first diverging state and the changed sections. Alignment beyond state index stays later |
| Proposals are not useful enough to review | The deck works with any generator. Measure the keep rate per region before tuning the generator |
| Compile-time slices do not move the recorded times | Measure both commands in [`gaps.md`](gaps.md) after each change |
| Self-hosting lags one release | Toolchain sources call builtins the newest `v*` release already emits |
| Reference counts miss Signal cycles | ASan corpus replay. The tree owns views. A view is reference counted. A list signal frees lists `View.each` never mounted |
| Mobile hardware stays unproven | Host and simulator proofs do not close it. See [`gaps.md`](gaps.md) |
| URLSession and Skia pixels have no fuzz home | `--live` replays host loopback. `--differential` compares structural dumps |
