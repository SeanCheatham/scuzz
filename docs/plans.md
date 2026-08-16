# Short-term plan

## Slice: Last-use release for `Stream.eval` IO

`Stream.emit` retains the value. `Stream.eval` still stores the IO node without a retain.

- Retain the IO in `sz_stream_eval`. Drop an owned IO after the call.
- Proof: compiler IR for `Stream.compileToList(Stream.eval(IO.pure("a")))` shows `sz_release` of the IO after `sz_stream_eval`.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
