# Arabic joining across font changes

Keep Arabic joining forms when the font or size changes inside a word.
Keep the same direction run in the visible line as shaping context for each font group.
Exclude hidden lines from this context.
Keep paragraph direction, source text, inline actions, wrapping, and ellipsis in agreement.

Use the pinned HarfBuzz joining data for browser boundary context.
Use a pinned Arabic font to keep virtual joiners with fallback letters.
Keep source controls and source offsets in the original text.
Check joined, non-joining, and transparent characters.
Compare native and browser pixels with independent joining forms.
Use the shaping application for exact native and evaluator replay.
Run sanitizer, renderer, and integration checks.

Other contextual scripts, mark placement across font changes, and full bidi caret affinity remain open.

Status: Twenty native Arabic cases pass with exact pixels. Direction controls and isolates limit joining context. They cover bold, italic, size changes, joining controls, transparent marks, medial forms, source preservation, and hard and soft line breaks. The native FFI and full UI suites pass. Normal, ASan, and UBSan runtime suites pass. The two additional override cases pass targeted normal and sanitizer checks. Chromium desktop and mobile checks pass twenty browser cases. Firefox medial joining remains open. Complete browser checks, application replay, renderer checks, and full integration.
