# Portable text shaping

Replace code-point cells for proportional text in the native software renderer.
Use the existing text span API and pinned font data.
Keep editor and monospace text on fixed cells.
Keep Headless as a required peer. Check Android with the same font path.

Measure and paint the same shaped glyphs.
Preserve source clusters, caret positions, selection, wrapping, and ellipsis.
Support kerning, ligatures, combining marks, and direction runs.
Keep paragraph context across wrapped lines.
Keep Arabic joining context across font and size changes.
Use the existing fallback faces for Arabic, Devanagari, Thai, and Simplified Chinese.
Preserve font styles, text color, clipping, opacity, and scale.

Use one portable shaping implementation where the software and OpenGL paths need it.
Do not add a second paragraph layout engine or a second font selection policy.
Keep the browser Canvas path and default Skia path as independent references.
Keep bitmap allocation bounded by the visible clip.
Release font and glyph resources when their owner ends.

Use the existing text and shaping applications.
Check native software pixels, metrics, clusters, and field input against independent references.
Check the OpenGL path after the software path works.
Reject a deliberate metric or glyph placement defect.
Compare exact native and evaluator timelines and claims.
Run native runtime, sanitizer, renderer, browser, and affected package checks.
Rebuild the CLI and check its GUI manual.
Run a finite Android emulator check. Keep unavailable Apple execution explicit.

Status: Native software and OpenGL use the shared native shaper.
The portable font provider uses verified Latin and fallback faces.
The glyph raster path uses HarfBuzz outlines and clipped coverage rows.
Measure, paint, clusters, and caret positions use the same glyphs.
Editor and monospace text keep fixed cells.
Line separators have no glyph bounds or advance.
The native build verifies the Latin and monospace files by SHA-256.
The build supplies standalone HarfBuzz source from its pinned archive.
Full runtime checks and paint checks pass for all three native renderers.
Android arm64 and x86_64 builds pass. The APK includes font and shaper licenses.
ASan and UBSan checks pass for the portable shaper, font provider, and raster path.
Independent Skia checks cover 48 script, size, and style cases.
They reject deliberate text advance and glyph placement defects.
Wider runtime parity, browser checks, package checks, and Android execution remain open. Apple SDK execution is unavailable on this host.
