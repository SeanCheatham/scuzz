# Portable monospace glyphs

Use the pinned DejaVu Sans Mono face in all native renderers.
Replace software and OpenGL bitmap glyphs with portable glyph outlines.
Keep the existing editor cell layout and source byte positions.
Use the same font data for measurement and paint.
Preserve clipping, styles, opacity, and scale.
Remove the unused ASCII bitmap font and its draw path.

Extend the independent Skia text check to monospace spans.
Compare advances, carets, metrics, clusters, and glyph coverage.
Reject deliberate metric and glyph placement defects.
Check native editor input and scaled text in software, OpenGL, and Skia.
Run affected runtime, sanitizer, package, browser, and replay checks.
Rebuild the CLI. Keep unavailable platform checks explicit.

Status: Inspect native monospace measurement and editor cell behavior.
