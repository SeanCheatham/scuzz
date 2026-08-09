# embedder-desktop (Phase 3)

OS window presentation for `UiRuntime.Window`. Headless remains the CI peer.

## Linux (X11)

`libscalui_embedder.a` opens a simple X11 window and blits RGBA frames after `pump`.
Linked by Stage 0 when present (`-lX11`).

- Requires `DISPLAY` (use `xvfb-run` in CI)
- Press `q` / Escape to close during `present`
- Without `DISPLAY`, Window stays offscreen

Build is Linux-only (`uname`); macOS/Windows Window presenters deferred — same session protocol.
