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
Keep Android system controls outside the Scuzz viewport.
Apply system, display cutout, and keyboard insets to the host container.
Check input against the visible controls.
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
- [Android animator preference](https://developer.android.com/reference/android/provider/Settings.Global#ANIMATOR_DURATION_SCALE)

Status: Native sessions support an explicit reduction setting and recorded preference events.
Linux has initial detection and live Settings portal input.
A private bus check covers repeated changes, invalid signals, foreign senders, stale replies, service restart, and shutdown.
It rejects a deliberate inversion of the preference.
The rebuilt product CLI includes the current runtime and GUI manual.
The motion campaign reaches all 29 triggers. It rejects four mutants and reports no survivors.
All 44 selected motion replays have equal native and evaluator timelines and claims.
Desktop input and resize checks pass.
Full UI, browser, and package integration checks remain open.
Recordings include the initial preference before live input.
A shared runtime check replays 20 cases with the opposite initial preference.
A native event fixture compares 400 Desktop and Mobile recordings across all five transition types.
It checks retained input, field selection, and the forced native setting.
The native replay check rejects a missing initial preference, stale suppression, and an ignored forced setting.
Preference updates clear inherited suppression before later input.
Normal runtime IO and UI checks pass.
Full ASan and UBSan runtime checks pass.
All 400 native replay cases pass in normal, ASan, and UBSan builds.
The private Linux Settings portal check also passes under both sanitizers.
Android reads the animation setting before the application worker starts.
It observes changes and reads the setting again on resume.
The Java source compiles. Android packages compile for both native ABIs.
The finite Android emulator check passes with a temporary app and restored settings.
It checks initial reduction, live changes, visible input, resume, and a new process.
The host container excludes the system bars and display cutout.
Keyboard-inset execution checks remain open.
macOS reads the workspace preference and observes its notification center.
iOS reads the accessibility preference and observes its status notification.
Both sources release their observers at shutdown. iOS also reads the setting on resume.
The existing Apple test programs include notification and session cleanup checks.
Apple SDK compilation and execution remain unverified.
The Linux host has D-Bus development files and tools for a finite private session bus.
Apple SDK tools are unavailable on this host. Apple execution checks remain open.
