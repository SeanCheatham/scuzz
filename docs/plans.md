# Next slice

Scuzz spans on diagnostics and panic (first half of gap 3 in [`gaps.md`](gaps.md)).

Today `check` diagnostics carry `Out.span` as pretty-printed expression text. The reporter finds it by substring search over the concatenated sources and hardcodes the file as `t/src/Main.scuzz`. LSP goto-def jumps to line 0. Rename rewrites whole files. Panic prints no Scuzz location.

## Done when (sub-slice 1: real diagnostic spans)

- The lexer records each token's start offset and file stem. Parse keeps the start offset on each def and on expressions that can fail (`ECall`, `EField`, `EBin`, `EMethod`, `EMatch`, `EFor`).
- `Out.span` becomes `(stem, offset)`. `check` reports the real file stem, line, and column. No substring search. No hardcoded file.
- JSON diagnostics keep the same schema. The editor maps diagnostics to the right file.
- `examples/studio` (multi-file) and `examples/bad-intent` keep their current behavior otherwise. Toolchain self-compiles green.

## Later sub-slices (not in this one)

- Panic carries the enclosing def's stem:line. Emit bakes the span of the panicking call into `sz_panic` text.
- LSP goto-def / rename / hover / completion land on the recorded spans.

## Out of slice

The release-gated LSP framing migration stays pending (a release must carry the byte kits first). Typed `E`, session schema, source-region coverage stay later.
