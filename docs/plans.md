# Short-term plan

## Next

**Watch-load a rebuild dylib** — the session can `dlopen` `sz_ui_reload_rebuild` and stamp-swap Views with new machine code. Next: `[ui]` `run --watch` rebuilds that dylib and calls `sz_ui_session_load_code` so source changes load new code without restarting. `watch` still only rebuilds.
