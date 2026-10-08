# Current slice: evaluator reliability and parity

Status: the server uses separate startup and response waits. An index reduces measured function lookup costs. Evaluator maps use a balanced tree. Map model, API, compiler, generated program, startup, IO signal, Headless UI, and search-feedback checks pass. Constructor field selection and View signal dumps have equal timelines and claims on selected replays. The Docs corpus passes. Inline bindings use names from their expression scopes. Generated proofs check repeated local names and closure calls in separate match arms. Module type identity remains open. The compiler reads only selected record fields. Source-offset lookup avoids a tuple allocation. Fixed-source warm probes show a measured decrease. Compiler idle costs remain open. Complete this slice before local choice feedback. Read `HUMANS.md` and the evaluator locks in `philosophy.md`.

## Outcome

Selected compiler and editor workloads run on the evaluator with the same results and timelines as compiled execution. Separate incorrect execution from costs that exceed the probe deadline.

## Work order

1. Profile the remaining compiler idle costs. Measure record field reads, value allocation, and release. Use fixed sources to compare each change. Measure startup and two probes separately. `scuzz fuzz --iterations 0 examples/tyck` and `scuzz fuzz --iterations 0 examples/codegen` preserve probe sources and requests under `build/fuzz/ev`.
2. Correct demonstrated costs. Keep the 20-second probe deadline, memory limits, scheduler limits, claims, and workload scope. Add the smallest proof for each correction.
3. Preserve exact IO and Headless UI parity, corpus replay, and bounded search.
4. Restore the next local choice feedback plan when this slice is complete.

## Required validation

Rebuild the product CLI after compiler or runtime changes. Run formatting and type checks on changed packages. Run selected evaluator and compiled probes in sequence. Run the affected compiler, runtime, and editor proofs. Run the generated compiler slice if evaluator behavior changes. Run `git diff --check`.

## Limits

A finite comparison does not prove all programs equal. Keep human usefulness and unavailable platform evidence open. Do not start local choice feedback in this slice.
