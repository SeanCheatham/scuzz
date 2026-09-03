# Plans

Next slice, after the next `v*` release ships (the bootstrap then emits `Str.byteLen` / `Str.byteSlice`):

- `examples/compiler/src/Emit.scuzz` `strArr`: switch `Str.len` to `Str.byteLen`. LLVM `[N x i8]` sizes count bytes.
- `examples/compiler/src/Lsp.scuzz` framing (`frame`, `fillTo`, `readMsg5`): switch `Str.len` / `Str.slice` / `Str.drop` to `Str.byteLen` / `Str.byteSlice`. `Content-Length` counts bytes.
- `examples/cli/src/Main.scuzz` `lspOk`: restore the `Lsp.frame("é")` pin (`Content-Length: 2`).
