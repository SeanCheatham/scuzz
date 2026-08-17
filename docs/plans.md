# Short-term plan

## Slice: RC stream nodes

Kit callback packs drop after unpack. Stream nodes are not reference-counted, so `Stream.compileToList` does not drop the stream graph.

- Give stream nodes `sz_retain` / `sz_release` like IO nodes.
- Drop an owned stream after `compileToList` / `drain` / `exists`.
- Proof: compiler IR for `Stream.drain(Stream.emit("a"))` shows `sz_release` of the stream after the call.
- Out of scope: OS threads, capture lists on `sz_view_free`.
