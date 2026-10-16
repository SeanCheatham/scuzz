# Arabic joining across font changes

Keep Arabic joining forms when the font or size changes inside a word.
Keep the complete visible line as shaping context for each font group.
Exclude hidden lines from this context.
Keep paragraph direction, source text, inline actions, wrapping, and ellipsis in agreement.

Use the pinned HarfBuzz joining data for browser boundary context.
Keep source controls and source offsets in the original text.
Check joined, non-joining, and transparent characters.
Compare native and browser pixels with independent joining forms.
Use the shaping application for exact native and evaluator replay.
Run sanitizer, renderer, and integration checks.

Other contextual scripts, mark placement across font changes, and full bidi caret affinity remain open.

Status: Thirteen native Arabic cases pass with exact pixels. They cover bold, italic, size changes, joining controls, transparent marks, medial forms, source preservation, and hard and soft line breaks. The native FFI and full UI suites pass. Add browser joining context, application replay, sanitizer checks, and full integration.
