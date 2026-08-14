# Short-term plan

## Next

**Prove a live View-label change** — `[ui] build` emits `build/reload.dylib` (`sz_ui_reload_rebuild`); `run --watch` recompiles it then stamps. Next: a Headless counter string change appears in `debug.dump` without restarting (Scuzz capture env vs new dylib). `watch` still only rebuilds.
