# Next slice

Nightly deep campaign.

CI runs a bounded deterministic campaign on push. Add a scheduled job that runs long-budget campaigns over the UI examples so deep search gets hours, not seconds. No corpus auto-commit: the nightly reports failures; corpus stays author-reviewed.

Proof:

- The workflow parses and the campaign commands pass locally at a reduced budget (`--iterations 128` over counter and studio).
- CI `linux-headless` stays green.

Current locks: [`vision.md`](vision.md). Ranked gaps: [`gaps.md`](gaps.md).
