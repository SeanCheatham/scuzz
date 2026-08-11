# embedder-desktop

OS window presentation for `UiRuntime.Window`. Headless remains the CI peer.

`libscuzz_embedder.a` opens a native window and blits RGBA frames after `pump`.
Linked by Stage 0 / Stage 1 / Stage 2 when present.

## Linux (X11)

- Requires `DISPLAY` (use `xvfb-run` in CI)
- Press `q` / Escape to close during `present`
- Without `DISPLAY`, Window stays offscreen
- Link: `-lX11`
- `sz_embedder_alive` is 0 after quit (stay-open apps stop pumping)

## macOS (Cocoa)

- Requires a GUI session (main display); otherwise Window stays offscreen
- Press `q` / Escape to close during `present`
- AppKit runs on the process main thread (`dispatch_sync` from the IO worker;
  `sz_runtime_main_args` parks main in the CFRunLoop)
- Link: `-framework Cocoa -lobjc` (apps also need `-framework CoreFoundation`)
