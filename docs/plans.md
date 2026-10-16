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
Check native and evaluator replay, renderers, sanitizers, and integration.

Status: Native same-font rich text retains paragraph context. Seven range cases match independent position and pixel references. Normal, ASan, UBSan, Skia, and software checks pass. Browser C compilation passes. Complete plain text, browser context, application replay, and full integration checks.
