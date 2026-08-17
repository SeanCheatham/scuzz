# Short-term plan

## Slice: Release Stream.filter / map / takeWhile capture lists

`Resource` and `Net.serve` callback envs drop when the owner frees. `Stream.filter` / `map` / `takeWhile` / `dropWhile` / `find` unpack the pack and leave the capture env on the stream without retain. Stream free already releases `env`.

- Retain the callback env in those combinators.
- Drop the construction ref of the capture list after packing the lambda.
- Proof: compiler IR for `Stream.drain(Stream.filter(Stream.emit("a"), x => tag))` with a captured `tag` shows `sz_release` of the capture list after the pack.
- Out of scope: OS threads.
