# Short-term plan

## Next

**Nested ADT patterns** — `check` rejects a non-exhaustive enum/record `match` unless `_` is present (`non-exhaustive match: missing Color.Blue` in the same JSON schema `scuzz lsp` already wraps). Next: nested payload patterns (`case Opt.Some(Color.Red)`) still bind names only.
