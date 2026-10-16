# Scale transforms

Add `View.scale(percent, child)` and `View.animatedScale(value, pumps, child)`.
Use an integer percentage. Use 100 for the original size.
Clamp display to 0 through Int32 maximum without writing the source Signal.
Scale about the child center.
Keep surrounding layout unchanged. Keep text wrapping unchanged.
Scale text, shapes, images, clips, and opacity layers through the shared paint path.
Keep ancestor clips effective.

Use the displayed geometry for pointer input, field selection, drag input, and control bounds.
Place tooltips and select popups from the displayed trigger bounds.
Keep keyboard focus and child state during positive scale changes.
Zero scale excludes child paint, input, focus, and accessibility. Keep child state available.
Check nested scales and composition with existing motion wrappers.

Use the shared pump clock and motion controls.
Use the initial Signal value without an entrance transition.
Start from the current scale when the target changes.
Layout and input do not advance the transition.
Pause preserves the current scale. Hidden or reduced content snaps to the target.
Expose current scale, target scale, and remaining pumps in recorded output.

Extend the motion application with an interactive scale sample.
Check independent pixels, input coordinates, retained fields, text, images, clipping, and source limits.
Compare exact native and evaluator timelines and claims.
Reject a deliberate paint or coordinate defect with an independent check.
Run runtime, sanitizer, renderer, browser, and integration checks.
Rebuild the product CLI and check its GUI manual.

Status: The paint API saves clips and opacity layers. It has no scale operation. Input functions use layout coordinates. Shared scale transforms remain to implement.
