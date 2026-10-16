# Direction in wrapped paragraphs

Resolve paragraph direction before line wrapping.
Keep paragraph context for each displayed line.
Use the same context for measurement, glyph positions, and paint.
Preserve source bytes, inline actions, selection, and explicit line breaks.
Keep font changes and full caret affinity as separate open work.

Check RTL and LTR paragraphs whose later lines start in the other direction.
Check numbers, punctuation, embedding controls, and explicit paragraph breaks.
Compare plain and same-font styled text.
Use native pixels, browser source ranges, and the shaping application.
Resolve browser canvas runs with the same paragraph direction algorithm.
Keep source spans in the same visual runs.
Check native and evaluator replay, renderers, sanitizers, and integration.

Status: Native plain and same-font rich text retain paragraph context. Seven range cases and eight plain-text cases match independent pixel references. Normal, ASan, and UBSan checks pass. Source labels and plain-text line heights stay correct. Complete browser context, application replay, renderer checks, and full integration checks.
