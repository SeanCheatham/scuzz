# Short-term plan

## Slice: Last-use release for `Stream.compileToList` streams

`List.filter` drops its input and closure pack. `Stream.compileToList` does not drop the stream.

- Drop an owned stream after `sz_stream_compile_to_list`.
- Proof: compiler IR for `Stream.compileToList(Stream.emits(["a"]))` shows `sz_release` of the stream after the call.
- Out of scope: cycle collector, OS threads, device packaging, View / Stream lambda packs that the graph still holds.
