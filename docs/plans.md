# Native accessibility

Expose shared control semantics to Android accessibility services.
Keep Headless and native input on the same action path.

The shared snapshot and action layer passes the runtime, ASan, and UBSan checks.
The focused ASan check also passes with leak detection enabled.
Android publication and recorded accessibility input remain open.

- Add owned semantic snapshots with View identities, parent links, clipped bounds, live values, state, and available actions.
- Validate actions against the current visible semantic tree. Reject removed, hidden, excluded, and background dialog targets.
- Queue Android actions to the runtime worker. Publish copied snapshots for platform reads.
- Add Android virtual nodes, focus, hover exploration, control actions, and change events. Preserve UTF-8 text and UTF-16 selection at the platform boundary.
- Record and replay accessibility input through the shared session path.
- Prove repeated labels, more than 64 controls, modal scope, transforms, clipping, reactive replacement, Unicode editing, and stale actions in Headless.
- Run a finite Android UI automation proof with real virtual-node actions. Keep real screen-reader and hardware checks explicit.
- Run affected runtime, sanitizer, replay, package, and Android checks. Rebuild the CLI and update the manual and current gaps.

The Android drawing surface has no virtual control children.
The shared runtime already supplies labels, state, geometry, focus, and stable View identities.
