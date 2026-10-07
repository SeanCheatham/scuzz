# Current slice: evaluator reliability and parity

Status: selected IO and editor parity checks pass. Runtime, AddressSanitizer, compiler, corpus, generated-case, and search-feedback checks pass. Compiler idle costs remain open. Complete this slice before local choice feedback. Read `HUMANS.md` and the evaluator locks in `philosophy.md`.

## Outcome

Selected compiler and editor workloads run on the evaluator with the same results and timelines as compiled execution. Separate incorrect execution from costs that exceed the probe deadline.

## Work order

1. Profile the compiler idle probes. Use the two-request measurements in `gaps.md` to separate startup from probe execution. `scuzz fuzz --iterations 0 examples/tyck` and `scuzz fuzz --iterations 0 examples/codegen` preserve probe sources and requests under `build/fuzz/ev`.
2. Correct demonstrated costs and the first-response timeout risk. Keep the 20-second probe deadline, memory limits, scheduler limits, claims, and workload scope. Add the smallest proof for each correction.
3. Preserve exact IO and Headless UI parity, corpus replay, and bounded search.
4. Restore the next local choice feedback plan when this slice is complete.

## Required validation

Rebuild the product CLI after compiler or runtime changes. Run formatting and type checks on changed packages. Run selected evaluator and compiled probes in sequence. Run the affected compiler, runtime, and editor proofs. Run the generated compiler slice if evaluator behavior changes. Run `git diff --check`.

## Limits

A finite comparison does not prove all programs equal. Keep human usefulness and unavailable platform evidence open. Do not start local choice feedback in this slice.
