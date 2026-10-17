# PR validation

Land PR #172 before new UI work.

The portable text build, compiler cache checks, C runtime suite, and exact View signal replay pass locally.
The product CLI rebuild passes.

- The full local kernel slice passes.
- The iOS network proof passes with nested text objects and the C++ runtime.
- Portable text builds and cache reuse pass locally in paths with spaces.
- Fix native dispatch for source module calls in UI kit namespaces. Check native and evaluator output.
- Complete the iOS source reload proof.
- Require successful hosted CI, including iOS and all Linux check jobs.
- Remove this plan when validation is complete.
- Merge the PR and check main.
