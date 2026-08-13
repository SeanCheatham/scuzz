# Short-term plan

## Next

**Live event inject.** `[ui] run --watch` writes a live structural dump (`build/debug.dump` / `SCUZZ_UI_DEBUG_DUMP`) so agents can read signals + a11y without killing the process. They cannot yet tap/type into a keep-alive session. Next: a stamp-driven inject file using the existing tap/text/pump script protocol. Still not Flutter VM patching. Do not document `watch` as hot reload.
