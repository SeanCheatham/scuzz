# scalui-ui (Phase 2+)

Design language: layout, widgets, theme tokens, gestures, animation, a11y hooks. Views are backend-agnostic under the `Ui` effect.

Phase 2–6 implementation lives in `crates/runtime` (`view.c`, `signal.c`, `theme.c`, `anim.c`, demos) so Stage-0 links a single `libscalui_rt.a`. Phase 6 adds theme polish tokens, `SuAnimFloat` (pump-ticked), and Headless-dumpable a11y metadata. This crate remains the future home when widgets move into ScalUI sources.
