# Next slice: in-memory diff side

Goal: the Timeline landmark compares the open buffers with the files on disk. It calls `Diff` in-process on two file sets. It does not start `scuzz diff`, `git`, or a native build. Direction: [`vision.md`](vision.md#primary-arc-proposal-review-in-the-ide) step 1. Locks: [`philosophy.md`](philosophy.md#proposal-review).

## Steps

1. Split `Diff.sideMan` so that a side takes a manifest and a file set. The directory path reads the files and calls the file set path. `scuzz diff` keeps its behavior.
2. Run a file set side on the evaluator. Use the probe path of `scuzz fuzz`: load the file set once, fork a child for each workload, and write the timeline dump under `build/ide/`. Workloads are the idle probe and the seeds. Corpus replay stays compiled in `scuzz diff`.
3. In the editor, build the disk file set as Check does. Build the buffer file set: the disk set with each dirty buffer in place of its file.
4. Add a Compare buffers control to the Timeline landmark. It calls the file set diff and feeds the report and timelines to `Lanes`. The Diff control keeps `scuzz diff` for a git revision.
5. Seed a package in `editor.scuzz_scenario`. Add a claim: after an edit that changes a label and a Compare buffers tap, the lanes show a diverging state with a changed `signals` section. Add a corpus entry.
6. Record the time from the Compare buffers tap to the lanes for the seeded package.

## Proof

- `./scripts/ci.sh ui`, `./scripts/ci.sh delta`, and `./scripts/ci.sh package` pass.
- The new claim fires and fails when its needle changes.
- The session heap oracle passes on the new corpus entry.

## Open questions

- Can a `[ui]` package fork an evaluator probe child from a tap handler fiber? If it cannot, run the probe server as the one subprocess and send it file sets on stdin.

## Status

Not started.
