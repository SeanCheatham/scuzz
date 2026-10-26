# Native accessibility

Expose shared control semantics to Android accessibility services.
Keep Headless and native input on the same action path.

Shared snapshots, action validation, and recorded input pass the runtime checks.
The Android transport passes the full runtime suite and focused ASan and UBSan checks.
The focused ASan check includes leak detection.
The Android provider and JNI compile. The product CLI rebuild and manual checks pass.
The Android package, motion preference, keyboard, resume, and restart checks pass.
The service query identifies and clears virtual input focus.
Native Backspace removes a selected emoji.
The keyboard proof checks exact text and caret offsets through five edit cycles.
It checks complete text retention and reset on restart.
A finite Android UI automation proof passes for virtual nodes and control actions.
Its captured Android record replays in Headless with equal state, text, selection, and semantics.

- Check touch exploration and transformed bounds at the platform boundary.
- Compare transformed and clipped semantic bounds with Android screenshots.
- Update current gaps and app instructions. Keep real screen-reader and hardware limits explicit.

New dialog controls send their initial focus event.
Hover-event routing passes in Android exploration mode.
Raw touchscreen conversion and real screen-reader gestures remain unproven.

The Android worker publishes copied semantics after layout.
The platform thread reads copied data and queues actions to the runtime worker.
The shared runtime validates each action against the current visible tree.
