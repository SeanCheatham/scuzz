# Next slice

UTF-8 `String`: `Str.*` indexes Unicode code points, not bytes.

Today `Str.charAt` returns a byte, `Str.len` counts bytes, and `Str.slice` / `take` / `drop` / `takeRight` / `dropRight` / `reverse` / `indexOf` / `lastIndexOf` work in bytes. Direction: code points. Gap 2: [`gaps.md`](gaps.md). Lock: [`vision.md`](vision.md#signal-string-and-errors).

## Done when

- `Str.len` counts code points. `Str.charAt` returns the code point at a code-point index (`-1` out of range). `Str.slice` / `take` / `drop` / `takeRight` / `dropRight` take code-point indices. `Str.reverse` reverses code points. `Str.indexOf` / `lastIndexOf` report code-point indices.
- New kit `Str.byteLen` keeps the byte count for protocol framing. Editor and compiler LSP `Content-Length` use it.
- Runtime byte ops stay for internal C (HTTP, sockets, JSON buffers).
- ASCII text behaves identically. A non-ASCII proof: an example or scratch package slices and counts a multibyte string.
- `scuzz check` / `test` / `fuzz --iterations 0` on the example suite match the pre-slice host baseline. Toolchain self-compile stays green.

## Out of slice

Case mapping stays ASCII (`Str.toLower` / `toUpper` / `capitalize`). TextField / editor caret stays byte offsets. Grapheme clusters, normalization, and collation are not kits.
