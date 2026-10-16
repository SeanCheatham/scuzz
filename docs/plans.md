# Content-size transitions

Add `View.animatedContentSize(pumps, child)`.
Measure the child with the incoming constraints.
Use the measured size as the target without width or height Signals.
Keep fractional layout dimensions and logical scale.
Use the initial measured size without an entrance transition.
Start from the current displayed size when the measured target changes.
Lay out the child at its measured size. Clip it to the displayed box.
Keep following content at the displayed box boundary.
Apply incoming minimum and maximum constraints to that box.

Use the shared pump clock and motion controls.
Layout and input do not advance the transition.
Pause preserves the displayed size.
Reduced motion uses the measured target immediately.
Hidden content cancels pending motion. Its next visible layout uses the current measured size.
Keep child state. Exclude zero-size content from focus and accessibility.
Expose current size, measured target, and remaining pumps in recorded output.

Extend the motion application with content changes that need no size Signals.
Check exact steps, retargeting, wrapped text, constraints, scale, clipping, and retained input.
Compare exact native and evaluator timelines and claims.
Reject a deliberate clipping or measurement defect with an independent check.
Run runtime, sanitizer, renderer, browser, and integration checks.
Rebuild the product CLI and check its GUI manual.

Status: The shared native constructor and recorded transition data are implemented. Focused native checks pass for geometry, pixels, pointer bounds, retargeting, fractional dimensions, wrapped text, constraints, retained input, hidden cancellation, pause, and reduced motion. Full normal, ASan, and UBSan runtime suites pass. An isolated clipping defect fails the independent pixel check. Compiler exposure, evaluator mount, application, claims, corpus, and manual are added. The CLI rebuild is active. Application checks, browser proof, replay, renderer checks, and integration remain.
