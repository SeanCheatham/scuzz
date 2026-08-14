# Short-term plan

## Next

**Emit a rebuild dylib on `[ui]` watch** — stamp-watch already `dlopen`s `SCUZZ_UI_RELOAD_CODE` (`build/reload.dylib`) then rebuilds Views. Next: clang-link that dylib (`define ptr @sz_ui_reload_rebuild` wrapping the rebuild lambda; same IR; Darwin `-undefined dynamic_lookup` / Linux `-shared`; do not link a second runtime) so source changes load new machine code without restarting. `watch` still only rebuilds.
