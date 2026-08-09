# ScalUI iOS packaging shell

Emitted by `scalui package --target ios`. Same `UiSession` protocol as Android /
host; UIKit maps touches and app lifecycle onto `SuInputEvent`.

## Layout

```
ScaluiAppDelegate.m   UIApplicationDelegate + CADisplayLink pump
Info.plist            bundle stub
```

## Build notes

1. Build `libscalui_rt.a` + `libsk_capi.a` for `iphoneos` / `iphonesimulator`.
2. Drop the emitted shell into an Xcode app target and link the archives.
3. Soft keyboard: TextField focus → `su_mobile_set_keyboard` → show `UITextField`
   shim that forwards characters as `SU_INPUT_TEXT`.

Simulator / device builds require Xcode; Headless goldens stay on the host.
