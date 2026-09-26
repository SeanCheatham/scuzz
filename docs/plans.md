# Next slice: region focus

Goal: the deck picks the next proposal from the same region of the code as the last decision. A Randomize control picks a new region. Direction: [`vision.md`](vision.md#primary-arc-proposal-review-in-the-ide) step 1. Locks: [`philosophy.md`](philosophy.md#proposal-review).

## Steps

1. Give each proposal a region. The region is the sorted list of stems that the proposal replaces.
2. After a decision, the deck prefers the next pending proposal in the region of the decided proposal. It falls back to directory order when the region is empty.
3. Add a Randomize control. It picks a pending proposal at random and reviews it next.
4. Add a claim in `chrome.scuzz_verify`: with proposals in two regions, a Keep takes the next proposal from the same region. A Randomize tap picks a pending proposal. Add corpus entries.

## Proof

- `./scripts/ci.sh ui` passes.
- The new claim fires and fails when its needle changes.
- The session heap oracle passes on the new corpus entries.

## Open questions

- None.

## Status

Not started.
