# Next slice

A3 — Hover and secondary click.

Today MOVE inject fails unless a button is down. There is no mouse button on `SzInputEvent`. Add a button field. Accept MOVE without a button. Show `View.tooltip` on hover. Headless inject drives hover and secondary click.

Each slice lands Headless inject + dump + a runtime test. Desktop and Headless share verbs.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
