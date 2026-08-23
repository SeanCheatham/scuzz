# Next slice

A6 — Viewport and monospace.

Scroll the viewport to the caret. Horizontal scroll for long lines. Vertical scroll for the file. Virtualize paint. Do not layout one View per line. Monospace typeface on every presenter (`sk_sw`, Skia CPU, GPU present). `View.fontSize` stays. The editor does not use the proportional UI font. `View.scroll` clips paint but does not window Views; the editor paints visible lines itself.

Each slice lands Headless inject + dump + a runtime test. Desktop and Headless share verbs.

Locks: [`vision.md`](vision.md). Full bar: [`gaps.md`](gaps.md).
