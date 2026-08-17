# Short-term plan

## Slice: Release Resource and Net.serve callback capture lists

Tap, `View.each`, `Signal.map`, and `Ui.run` rebuild env lists drop when the owner frees. `Resource.make` / `Resource.use` and `Net.serve` / `Net.serveOnce` unpack the callback pack and leave the capture env on the resource or server without retain or release.

- Retain the callback env when the resource or server stores it.
- Release that env when the resource or server frees.
- Proof: `Resource.use` with a captured String still prints after the factory returns, and free returns live_count to baseline.
- Out of scope: OS threads.
