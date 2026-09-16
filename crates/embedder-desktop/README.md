# embedder-desktop

OS window presentation for `UiRuntime.Desktop`. Headless remains the CI peer.

`libscuzz_embedder.a` opens a native window and blits RGBA frames after `pump`.
Linked when present.

## Linux (X11)

- Needs `DISPLAY` (use `xvfb-run` in CI)
- Close the window to quit during `present`
- Without `DISPLAY`, Desktop stays offscreen
- Link: `-lX11`
- `sz_embedder_alive` is 0 after quit (stay-open apps stop pumping)

## macOS (Cocoa)

- Needs a GUI session (main display). Otherwise Desktop stays offscreen.
- Close the window to quit during `present`
- AppKit runs on the process main thread (`dispatch_sync` from the IO worker;
  `sz_runtime_main_args` parks main in the CFRunLoop)
- Retina: session paints at `backingScaleFactor` and presents a point-sized
  `NSImage` with a matching pixel buffer (no upsample blur)
- Link: `-framework Cocoa -lobjc` (apps also need `-framework CoreFoundation`)

On macOS, `scuzz package --target macos` builds a local UI `.app` bundle.
The bundle includes its non-system libraries and an ad hoc signature.
Finder launch uses Desktop and reads UI size from the bundle metadata.
Explicit runtime environment values take priority.
Run `./scripts/ci.sh macos-app` to prove relocation and Finder launch.
