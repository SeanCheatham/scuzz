# Next slice

C2 — Check in-app.

Live: write buffer, `Sys.exec` `scuzz check --message-format=json`, `Json.parse`, diagnostics list, jump to caret. Fuzz: `*.scuzz_sim` overlay or canned JSON on mem FS. Do not stub `Sys.exec`. No second typer.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
