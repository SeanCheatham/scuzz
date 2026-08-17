# Short-term plan

## Slice: Last-use release for Stream, Resource, and Net.serve packs

`View.each`, `Signal.map`, and `Ui.run` drop the wrapper list after unpack. `Stream.filter` / `Stream.map` / `Stream.evalMap`, `Resource.make` / `Resource.use`, and `Net.serve` still leave `cons(fn, cons(env, nil))`.

- Mark those packs owned and drop them after unpack.
- Proof: compiler IR for `Stream.drain(Stream.filter(Stream.emit("a"), x => true))` shows `sz_release` of the filter pack after the call.
- Apply the same drop to `Stream.map`, `Stream.evalMap`, `Resource.make`, `Resource.use`, and `Net.serve` / `Net.serveOnce`.
- Out of scope: RC stream nodes, OS threads, capture lists on `sz_view_free`.
