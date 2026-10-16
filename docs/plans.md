# Native motion preferences

Read the operating-system motion preference for native UI sessions.
Apply the initial preference before the first transition can advance.
Send later preference changes through `SZ_INPUT_REDUCED_MOTION`.
Keep one shared motion policy and one recorded event format.
Application reduction and an ancestor reduction remain effective.
Keep the native environment setting available. Keep Headless replay deterministic.
Do not read host preferences during Headless or simulated runs.

Use the Linux Settings portal for `org.freedesktop.appearance` `reduced-motion`.
Use the session bus. Keep requests bounded. Release the connection at shutdown.
An absent service, absent key, or invalid reply must not prevent application use.
Check service restart, preference changes, unrelated signals, and stale replies.
Do not start a persistent service for checks.

Use the macOS workspace preference and its display-options notification.
Use the iOS accessibility preference and its motion-status notification.
Use the Android animator setting with change and resume handling.
Release observers when the native session ends.
Keep unavailable platform execution checks explicit.

Use the motion application to check cancellation and retained input.
Check opacity, size, content size, position, and scale transitions.
Replay recorded preference changes with the native runtime and evaluator.
Compare exact timelines and claims. Reject a missing preference update.
Check initial reduction, return to normal motion, pause, reload, and shutdown.
Run native runtime, sanitizer, embedder, browser, and affected package checks.
Rebuild the CLI. Update its GUI manual and the existing host setup instructions.

Sources:

- [Linux Settings portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html)
- [macOS workspace preference](https://developer.apple.com/documentation/appkit/nsworkspace/accessibilitydisplayshouldreducemotion)
- [macOS preference notification](https://developer.apple.com/documentation/appkit/nsworkspace/accessibilitydisplayoptionsdidchangenotification)
- [iOS preference](https://developer.apple.com/documentation/uikit/uiaccessibility/isreducemotionenabled)
- [iOS preference notification](https://developer.apple.com/documentation/uikit/uiaccessibility/reducemotionstatusdidchangenotification)
- [Android animator preference](https://developer.android.com/reference/android/animation/ValueAnimator#areAnimatorsEnabled())

Status: Native sessions support an explicit reduction setting and recorded preference events.
Linux has initial detection and live Settings portal input.
A private bus check covers repeated changes, invalid signals, foreign senders, stale replies, service restart, and shutdown.
It rejects a deliberate inversion of the preference.
Rebuild the product CLI after the shared runtime checks.
Desktop input and resize checks pass.
Runtime, replay, sanitizer, and package checks remain open for this change.
Recordings include the initial preference before live input.
A shared runtime check replays 20 cases with the opposite initial preference.
A native event fixture compares 100 recordings across all five transition types.
It checks retained input and field selection.
The native replay check rejects a missing initial preference and stale suppression.
Preference updates clear inherited suppression before later input.
Normal runtime IO and UI checks pass.
ASan and UBSan checks remain open.
Other native preference sources remain open.
The Linux host has D-Bus development files and tools for a finite private session bus.
Apple SDK tools are unavailable on this host. Apple execution checks remain open.
