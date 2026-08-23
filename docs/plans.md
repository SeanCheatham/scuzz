# Next slice

A5 — `View.editor`.

Multiline buffer on a `SignalStr`. Insert/delete at caret including newline and tab/soft-tab. No 256-byte stack cap. Headless `[editor]` dump: buffer, caret, selection. Do not route a file through `View.each` + `View.text`. Repeat/IME stay deferred unless they block the editor.

Each slice lands Headless inject + dump + a runtime test. Desktop and Headless share verbs.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
