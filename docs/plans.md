# Short-term plan

## Next

**CLI keep-alive on stamp-watch.** UI examples use `Ui.run(_ => View)` so construction can re-run. Next: `run --watch` keeps the process and reloads the View tree without resetting Signals. Still not Flutter VM patching. Do not document `watch` as hot reload.
