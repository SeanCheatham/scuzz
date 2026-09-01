# Next slice

Migrate LSP framing to the byte kits (release-gated).

`Str.len` now counts code points. The LSP framing in `examples/compiler/src/Lsp.scuzz` and `examples/editor/src/Lsp.scuzz` still uses `Str.len` / `Str.slice` for `Content-Length`, which is a byte count. The bootstrap compiler is the newest GitHub release, so the sources cannot name `Str.byteLen` / `Str.byteSlice` until a release carries them.

## Done when

- A `v*` release exists whose `scuzz` emits the UTF-8 kits.
- `frame()` uses `Str.byteLen(body)`. The read paths use `Str.byteLen` for `fillTo` and `Str.byteSlice` for the body cut.
- `scuzz check` on the toolchain stays green before and after.

## Out of slice

Everything else. Do not start thesis-critical gap work from this note; gap order lives in [`gaps.md`](gaps.md).
