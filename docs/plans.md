# PR validation

Land PR #172 before new UI work.

- The portable text build passes with repeated warning flags. Hosted iOS proof remains required.
- View replay exposes an old enum offset. The compiler supplies the exact tag. Runtime checks and rebuilt replay remain required.
- The compiler cache identity and output directory checks pass. The check uses unlinked IR without a cache stamp.
- Run affected local checks. Rebuild the CLI when product source changes.
- Require successful hosted CI. Merge the PR and check main.

The current hosted run has three failed checks. Its application check remains active.
