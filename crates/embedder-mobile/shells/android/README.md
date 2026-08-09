# ScalUI Android packaging shell

Emitted by `scalui package --target android`. Links the shared ScalUI runtime +
Skia CPU backend; maps Android touch / lifecycle / IME onto `SuInputEvent`.

## Layout

```
jni/scalui_jni.c     JNI bridge → su_ui_* / su_mobile_*
AndroidManifest.xml  minimal activity
build.gradle         stub (wire NDK paths locally)
```

## Build notes

1. Build `libscalui_rt.a` + `libsk_capi.a` for your NDK ABI.
2. Point `local.properties` / `ndk.dir` at an Android NDK.
3. The activity owns: mount → (poll inject → pump → present)* → unmount.

Device / emulator builds require an NDK; Headless goldens stay on the host.
