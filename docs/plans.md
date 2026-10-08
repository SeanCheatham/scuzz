# Current slice: local choice feedback

Status: review records store choices and Undo operations. Generation requests include recent automatic failures and exclusions. They do not include developer preferences. Abstention reasons, region controls, and the local session summary remain open. Compiler idle costs and module type identity remain open in `gaps.md`. They do not prevent this slice. Read `HUMANS.md` and the proposal review locks in `philosophy.md`.

## Outcome

The next suggestion uses bounded feedback from the developer's choices. A local summary shows the results and time costs of the session. Both local model requests and exported requests use the same feedback.

## Work order

1. Read preferences from the primary durable review records. Include recent choices, abstention reasons, and Undo results in generation requests. Bound record count and bytes. Preserve the request identity and frozen baseline. Keep automatic exclusions separate from developer choices.
2. Add optional reasons for Can't decide. Show the reveal and a short result after a choice. Let the developer retain region focus or select another region. Keep the next card's lane mapping hidden.
3. Add the local session summary. Count accepted choices, baseline choices, abstentions by reason, exclusions, duplicates, and Undo. Report readiness time, review time, and waiting time separately. Count an acceptance as retained only when it is not undone and its touched files still match its accepted bytes at session end. Derive totals from primary records. Do not add a second decision log.
4. Prove request feedback, source retention, timing, pause, restart, and duplicate decision handling. Update `scuzz docs ide` to match the shipped controls.

## Required validation

Run formatting and type checks on changed packages. Run the editor simulation campaign and the affected Headless review proofs in `./scripts/ci.sh ui` and `./scripts/ci.sh ui-test`. Preserve exact editor replay and existing acceptance, recovery, and request publication checks. Rebuild the product CLI if shared compiler code changes. Run `git diff --check`.

## Limits

Do not start general compiler speed work in this slice. Correct a demonstrated application or proof blocker when necessary. Keep the current probe limits and evidence requirements. Do not add telemetry or model training. Source retention does not prove that later edits preserve intended behavior. Automated choices do not establish human usefulness. Rule review follows this slice.
