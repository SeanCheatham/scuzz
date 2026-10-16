# Size transitions

Add `View.animatedSize(width, height, pumps, child)`.
Use width and height Signals in logical pixels.
Clamp each source value to 0 through Int32 maximum without writing its Signal.
Use one pump count for both dimensions.
Start from the current size when either target changes.
Keep child state and focus while its size remains visible.
Clip paint and pointer input to the current size.
Exclude zero-size content from focus and accessibility.

Use the shared motion clock for Headless, browser, Desktop, and Mobile.
Pause with the session. Snap to the target when motion is reduced or content is hidden.
Expose current size, target size, and remaining pumps in recorded output.
Extend the motion application and compare exact native and evaluator replay.
Check independent geometry, pixels, input, cancellation, and range limits.
Run sanitizer, browser, renderer, and integration checks.

Transform transitions and native operating-system motion preferences remain open.

Status: Source inspection confirms that shared transitions support opacity. Size implementation remains.
