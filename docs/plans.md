# Next slice

Typed fail `E` on `IO` (gap 4 in [`gaps.md`](gaps.md)).

`IO[T]` still fails with a `String` message. Direction: typed `E` without environment `R`. Do not add `ZIO[R, E, A]`.

## Done when

- Check encodes `IO[E, A]`. `IO[A]` means `IO[String, A]`.
- `IO.fail(e)` takes `E`. `handleErrorWith` binds `E`. `flatMap` keeps one `E`.
- `examples/tyck` pins a user enum as `E` and a String/`E` mismatch.
- Existing `IO[Unit]` programs still typecheck. Kits still fail with `String`.

## Out of slice

Session schema, source-region coverage, and changing the C `SzError` wire stay later.
