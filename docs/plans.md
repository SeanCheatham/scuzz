# Shaping across paint spans

Keep one shaping run across adjacent spans with the same font and size.
Color, underline, and inline actions do not split that run.
Use the complete run for glyph positions, wrapping, and paint.
Keep source text, selection, and action input correct across span boundaries.
Keep font changes explicit. Check their context limits separately.

Check kerning, ligatures, joined Arabic text, combining marks, and fallback fonts.
Compare plain text with the same text split into paint spans.
Check native pixels, browser source ranges, and inline actions.
Use the shaping application and exact native and evaluator replay.
Run renderer, runtime, sanitizer, browser, package, and clean UI checks.

Status: Normal, ASan, and UBSan runtime checks pass. Skia, software, and OpenGL paint checks pass. The 25 style and source-cluster cases preserve glyph coverage. Wrapped-action browser checks pass in all five browser configurations. Full browser checks run next. Complete application replay and full integration checks.
