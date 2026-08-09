# embedder-desktop (Phase 1+)

`UiRuntime.Window` is already a **peer interpreter** on the same session protocol as Headless (`su_ui_mount` with `SU_UI_RUNTIME_WINDOW` in `crates/runtime`). Phase 1 paints that peer offscreen so CI needs no display.

This crate is the home for **OS window presentation** (X11/Wayland/etc.) that will attach a real surface to the existing session without Window-only features.
