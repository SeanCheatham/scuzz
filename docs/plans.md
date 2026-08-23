# Next slice

A7 — Undo, gutter, and highlight.

Undo and redo in the editor widget. Gutter line numbers and diagnostic marks stay inside `[editor]`. Do not dump a second tree. Syntax highlight from LSP semantic tokens or an in-widget lexer. Folding, inlays, and bracket match may land here if they stay cheap. Do not block C1 on them.

Each slice lands Headless inject + dump + a runtime test. Desktop and Headless share verbs.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
