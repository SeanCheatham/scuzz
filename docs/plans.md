# Native accessibility

Expose shared control semantics to Android accessibility services.
Keep Headless and native input on the same action path.

Shared snapshots, action validation, and recorded input pass the runtime checks.
The Android transport passes the full runtime suite and focused ASan and UBSan checks.
The focused ASan check includes leak detection.
The Android provider and JNI compile. The product CLI rebuild passes.
A finite Android UI automation proof passes for virtual nodes and control actions.

- Check touch exploration, transformed bounds, and native input-focus lookup at the platform boundary.
- Preserve shared record and replay behavior. Run affected package and motion checks.
- Update current gaps and app instructions. Keep real screen-reader and hardware limits explicit.

The Android worker publishes copied semantics after layout.
The platform thread reads copied data and queues actions to the runtime worker.
The shared runtime validates each action against the current visible tree.
