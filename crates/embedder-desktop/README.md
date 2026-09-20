# embedder-desktop

OS window presentation for `UiRuntime.Desktop`. Headless remains the CI peer.

`libscuzz_embedder.a` opens a native window and blits RGBA frames after `pump`.
Linked when present.

- Linux (X11): `-lX11`. `sz_embedder_alive` is 0 after quit (stay-open apps
  stop pumping)
- macOS (Cocoa): `-framework Cocoa -lobjc` (apps also need `-framework
  CoreFoundation`). AppKit runs on the process main thread (`dispatch_sync`
  from the IO worker; `sz_runtime_main_args` parks main in the CFRunLoop)

App behavior and packaging: `scuzz docs gui` and `scuzz docs packages`.
