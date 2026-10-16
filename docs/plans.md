# Visual order across font changes

Keep the visual order of each rich-text line when its font or size changes.
Use complete-line source positions to place font groups.
Keep paint, source ranges, inline actions, wrapping, and ellipsis in agreement.
Keep software and OpenGL cell behavior explicit.

Check RTL and LTR lines with bold, italic, and size changes.
Include mixed-direction groups, spaces, wrapped lines, and ellipsis.
Compare native pixels with independently placed runs.
Check browser source geometry, copying, and action input.
Use the shaping application for exact native and evaluator replay.
Run sanitizer, renderer, and integration checks.

Contextual glyph forms across font changes and full bidi caret affinity remain open.

Status: Eight native pixel cases pass. Eight browser font-change cases pass in all five configurations. The twelve browser paragraph cases also pass. Normal and sanitizer checks pass. Browser copying, source geometry, retained input, and the changed-font action pass. All five application corpus files and all six triggers pass. Four mutations are rejected. One mutation is inert. One mutation is invalid. No mutation survives. Exact native and evaluator replay passes for keyboard, pointer, paragraph, and font-change input. Software, Skia, and OpenGL checks pass. The software UI suite also passes. Complete full Web, package, and clean Headless integration checks.
