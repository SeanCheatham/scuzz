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

Status: Twenty native Arabic cases pass with exact pixels. They cover bold, italic, size changes, direction controls, isolates, transparent marks, medial forms, source preservation, and line breaks. The FFI, normal runtime, ASan, and UBSan suites pass. Targeted sanitizer checks cover the two additional override cases. The renderer checks pass and the default Skia renderer is restored. The 16-iteration shaping campaign and five exact native and evaluator replays pass. Chromium desktop, Chromium mobile, and Firefox desktop checks pass twenty browser cases. WebKit pixel checks remain open. Complete browser checks and full integration.
