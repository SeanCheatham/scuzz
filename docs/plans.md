# Direction in wrapped paragraphs

Resolve paragraph direction before line wrapping.
Keep paragraph context for each displayed line.
Use the same context for measurement, glyph positions, and paint.
Preserve source bytes, inline actions, selection, and explicit line breaks.
Keep font changes and full caret affinity as separate open work.

Check RTL and LTR paragraphs whose later lines start in the other direction.
Check numbers, punctuation, embedding controls, and explicit paragraph breaks.
Compare plain and same-font styled text.
Keep paragraph context for ellipsis in RTL rich text.
Use native pixels, browser source ranges, and the shaping application.
Resolve browser canvas runs with the same paragraph direction algorithm.
Keep source spans in the same visual runs.
Check native and evaluator replay, renderers, sanitizers, and integration.

Status: Twelve browser paragraph cases pass in all five browser configurations. These cases include plain-text and rich-text RTL ellipsis. Source selection, full and partial copying, and the wrapped action pass. Native range, bracket, and ten rich-text ellipsis pixel cases pass. Normal, ASan, and UBSan runtime suites pass. The resize corpus and exact native and evaluator replay pass. The shaping campaign rejects five mutations. One mutation is inert. No mutation survives. The release package check passes. Software, Skia, and OpenGL paint checks pass. Software and Skia UI suites pass. Complete full Web, package, and clean Headless integration checks.
