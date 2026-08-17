# Short-term plan

## Slice: Release View.each, Signal.map, and Ui.run capture lists

Tap env lists drop in `sz_view_free`. `View.each` mappers, `Signal.map`, and `Ui.run` rebuild packs unpack and drop the wrapper, then leave the capture env on the owner without retain or release.

- Retain `each_env` / `map_env` / `rebuild_env` when the owner stores it.
- Release those envs when the View, mapped Signal, or session frees.
- Proof: Headless `View.each` with a captured Signal still builds rows after the factory returns, and free returns live_count to baseline.
- Out of scope: OS threads.
