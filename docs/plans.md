# PR validation

Land PR #172 before new UI work.

- Fix the iOS third-party text build flags. Check the affected compile command.
- Reproduce and fix exact View signal replay. Preserve timeline and claim checks.
- Reproduce and fix the compiler cache check. Preserve output directory ownership.
- Run affected local checks. Rebuild the CLI when product source changes.
- Require successful hosted CI. Merge the PR and check main.

The current hosted run has three failed checks. Its application check remains active.
