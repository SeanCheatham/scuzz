# embedder-desktop (Phase 3)

OS window presentation for `UiRuntime.Window`. Headless remains the CI peer.

`libscalui_embedder.a` opens a native window and blits RGBA frames after `pump`.
Linked by Stage 0 / Stage 1 when present.

## Linux (X11)

- Requires `DISPLAY` (use `xvfb-run` in CI)
- Press `q` / Escape to close during `present`
- Without `DISPLAY`, Window stays offscreen
- Link: `-lX11`
- `su_embedder_alive` is 0 after quit (stay-open demos stop pumping)

## macOS (Cocoa)

- Requires a GUI session (main display); otherwise Window stays offscreen
- Press `q` / Escape to close during `present`
- Link: `-framework Cocoa -lobjc`
- `su_embedder_alive` is 0 after quit (or closing the window)

## Other hosts

Build is skipped (`uname`); Headless/Mobile still work. Windows presenter deferred — same session protocol.
