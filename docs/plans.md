# Next slice

B1 — `Fs.list` entries and path helpers.

`Fs.list` returns `IO[List[(String, Bool)]]` (`name`, `isDir`). Mem FS already stores `is_dir`. Add `Fs.exists`. Add `Fs.join` / `Fs.dirname` / `Fs.basename`. Forwards-only: rewrite every `IO[List[String]]` call site. Live disk and TestRuntime stay in lockstep.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
