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

Status: The portable provider uses pinned DejaVu Sans Mono outlines.
Native monospace metrics use fractional advances.
The 168-row independent reference check passes.
It rejects deliberate metric and glyph placement defects.
It includes fractional sizes, Greek, and Cyrillic text.
Renderer and UI runtime checks pass for software, OpenGL, and Skia.
Scale checks compare monospace spans and editor cells with larger glyphs.
Caret paint uses a device pixel column. Reported caret geometry keeps fractional values.
Runtime ASan and UBSan checks pass.
Portable font, shaper, and raster checks pass under ASan and UBSan with leak detection.
Unicode editor checks cover pointer positions, arrow movement, insertion, and UTF-8 byte offsets.
They pass for software, OpenGL, Skia, ASan, and UBSan at two display scales.
The CLI rebuild passes.
Text application replay passes for software, OpenGL, and Skia.
Native and evaluator timelines, claims, source text, control geometry, and field state match exactly.
Package CI passes. Browser text and control checks pass in all five configurations.
Android arm64 and x86_64 packages pass.
A finite Android API 36 emulator check covers monospace spans, counter input, typing,
keyboard viewport changes, and retained state after resume.
The temporary app, emulator, and private ADB server are removed or stopped.
Software and OpenGL application snapshots have a maximum channel delta of 1.
Clean UI validation is in progress.
Apple SDK execution and physical-device checks remain unverified.
