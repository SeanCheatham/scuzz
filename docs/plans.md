# Next slice: step view

Goal: a tap on a step in the Timeline lanes renders the `View` of that side at that state. Today the lanes show dump sections as text. Direction: [`vision.md`](vision.md#primary-arc-proposal-review-in-the-ide) step 1. Locks: [`philosophy.md`](philosophy.md#proposal-review).

## Steps

1. Reproduce the `View` at a recorded state. The timeline dump records the view tree at each state. Read the tree for the chosen step from the lane timeline. Do not replay the side.
2. Render the tree in the lane next to the dump sections. Headless paints it like any other `View`.
3. Add a claim in `chrome.scuzz_verify`: after a step tap, the lane shows the rendered view of that state. Add a corpus entry.

## Proof

- `./scripts/ci.sh ui` and `./scripts/ci.sh delta` pass.
- The new claim fires and fails when its needle changes.
- The session heap oracle passes on the new corpus entry.

## Open questions

- Does the timeline dump keep the full view tree at every state, or only at checkpoints? If it keeps checkpoints only, render the nearest checkpoint and mark the gap.

## Status

Not started.
