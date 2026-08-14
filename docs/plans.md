# Short-term plan

## Next

**Dump bindText in `[views]`** — `sz_view_text_signal_str` has no a11y role, so mapped labels are missing from `debug.dump` (goldens skip `count = 0` / `taps = 0`). Next: emit the live string so agents can read Signal-bound text. `watch` still only rebuilds.
