# PR validation

Land PR #172 before new UI work.

The portable text build, compiler cache checks, C runtime suite, and exact View signal replay pass locally.
The product CLI rebuild passes.

- The full local kernel slice passes.
- Check the iOS proof link with nested text objects and the C++ runtime.
- Require successful hosted CI, including iOS and all Linux check jobs.
- Remove this plan when validation is complete.
- Merge the PR and check main.
