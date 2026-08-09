# scalui-ui (Phase 2+)

Design language: layout, widgets, theme tokens, gestures. Views are backend-agnostic under the `Ui` effect.

Phase 2–5 implementation lives in `crates/runtime` (`view.c`, `signal.c`, `theme.c`, demos) so Stage-0 links a single `libscalui_rt.a`. Pointer / scroll / soft-keyboard / lifecycle inject landed in Phase 5 (Headless-first). This crate remains the future home when widgets move into ScalUI sources.
