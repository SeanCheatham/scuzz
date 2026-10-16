# Position transitions

Add `View.animatedPosition(x, y, pumps, child)`.
Use two `Signal[Int]` coordinates in logical pixels.
Use the layout rules of `View.positioned`.
Clamp each coordinate to 0 through Int32 maximum without writing its Signal.
Use the initial coordinates without an entrance transition.
Use one positive pump count for both coordinates.
Start from the current position when either target changes.

Use the current position for child layout, paint, pointer input, and accessibility bounds.
Keep child state and focus during motion.
Check incoming constraints and parent clipping.
Use the shared motion clock and motion controls.
Layout and input do not advance the transition.
Pause preserves the current position. Hidden or reduced content snaps to the target.
Expose current coordinates, target coordinates, and remaining pumps in recorded output.

Extend the motion application with an interactive card in a bounded Stack.
Check both axes, retargeting, retained fields, scale, clipping, and signed 64-bit source limits.
Check independent pixels and pointer input at the old and current locations.
Compare exact native and evaluator timelines and claims.
Reject a deliberate position or input defect with an independent check.
Run runtime, sanitizer, renderer, browser, and integration checks.
Rebuild the product CLI and check its GUI manual.

Status: The shared native constructor and recorded position data are implemented. Focused native checks pass for exact steps, both axes, retargeting, pixels, pointer input, retained selection, scale, clipping, source limits, hidden cancellation, pause, and reduced motion. Full normal, ASan, and UBSan runtime suites pass. An isolated input defect fails the independent pointer check. Compiler exposure, evaluator mount, application, claims, corpus, and manual are added. The CLI rebuild and GUI manual check pass. The position corpus and motion campaign pass. All six sampled mutations are killed. All 33 exact native and evaluator replays pass. Position, content-size, size, and opacity browser checks pass in all five configurations. Browser range checks pass for both signed 64-bit limits and preserve the source values. Skia, software, and Headless OpenGL checks pass. Skia is restored. Full Web checks pass. Release-package and clean UI integration remain.
