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

Status: 24 native and browser cases check font changes, direction controls, isolates, marks, line breaks, and ellipsis. Empty isolates block joining with one font or two fonts. Native pixels match independent forms. Chromium, Firefox, and WebKit pass. Chromium and WebKit mobile emulation pass. The normal runtime and FFI suites pass. Targeted ASan and UBSan checks pass. Renderer checks pass with Skia restored. The 16-iteration shaping campaign and five exact native and evaluator replays pass. Inline checks reject redirected taps and releases outside visible action fragments. Full Web checks pass. Native host packages include the Arabic font license. The macOS resource copy check passes with platform commands mocked. Full release-package checks pass. Clean UI integration remains.
