# PR validation

Land PR #172 before new UI work.

The portable text build, compiler cache checks, C runtime suite, and exact View signal replay pass locally.
The product CLI rebuild passes.

- Complete the full local kernel slice.
- Require successful hosted CI, including iOS and all Linux check jobs.
- Remove this plan when validation is complete.
- Merge the PR and check main.
