# Next slice: in-process LSP

Goal: hover, goto-def, and rename call compiler modules in the IDE process. They do not start `scuzz lsp`. Direction: [`vision.md`](vision.md#primary-arc-proposal-review-in-the-ide) step 1. Locks: [`philosophy.md`](philosophy.md#proposal-review) and the dogfood IDE lock: the IDE calls compiler modules in-process, not CLI commands.

## Steps

1. Read how `LspClient.lspCall` is used for hover, goto-def, and rename. Read what `Lsp.scuzz` exposes for those methods.
2. Add a compiler-module entry that answers hover, definition, and rename from a checked file set. One typer: the compiler `Check` module.
3. Move the editor's hover, goto-def, and rename to the in-process entry. Completion, formatting, code actions, semantic tokens, inlay hints, and folding stay on `scuzz lsp` until a second step needs them.
4. Add a claim in `chrome.scuzz_verify` for the in-process answers. Add a corpus entry.

## Proof

- `./scripts/ci.sh ui` passes.
- The new claim fires and fails when its needle changes.
- The session heap oracle passes on the new corpus entry.

## Open questions

- Which results must stay span-exact with `scuzz lsp`? Hover, goto-def, and rename must use Scuzz source spans in both paths.

## Status

Not started.
