# embedder-desktop (Phase 3)

OS window presentation for `UiRuntime.Window`. Headless remains the CI peer.

## Linux (X11)

`libscalui_embedder.a` opens a simple X11 window and blits RGBA frames from the
runtime's offscreen Skia surface after `pump`. Linked automatically by Stage 0
when the archive is present (`-lX11`).

- Requires `DISPLAY` (use `xvfb-run` in CI if exercising Window presentation)
- Press `q` / Escape to close during `present`
- Without DISPLAY, Window stays offscreen (same as Phase 1/2)

Windows embedder is deferred (same session protocol).
