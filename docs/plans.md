# Next slice

B3 — `Sys.exec` capture `(code, stdout, stderr)`.

Capture via `dup2`. Still fail under TestRuntime. No exec stub map. Rewrite `IO[Int]` call sites. Forwards-only.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
