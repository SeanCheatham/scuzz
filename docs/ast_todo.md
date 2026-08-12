# AST / IR payload-ADT rewrites

Compiler-scuzz AST/IR families are payload ADTs (`Tok`, `InterpPart`, `ForBinder`, `Pattern`, `MatchArm`, `Span`, `EnumField`/`EnumCase`/`EnumDef`, `Param`/`Def`/`Prog`, `Expr`, `Type`/`TyRes`/`TyBind`, `Emit`/`EmitKind`/`EnvBind`/`CasePayload`; see [`gaps.md`](gaps.md)). Gate changes with `scripts/selfhost.sh` (incl. Stage-2/3 byte-identical IR and `fmt --check`).

Payload field types: `Int`, `String`, `List`, or a nominal ADT. Child exprs use typed `Expr` (or `List` of `Expr`) rather than List+string-tag nodes. Types use the `Type` ADT (not `"Adt:…"` / `"IO[…]"` strings). Codegen emit state uses `Emit` / `EmitKind` / `EnvBind` rather than List quads and `name=value:kind` rows.

## Done

| Family | Module | Notes |
| --- | --- | --- |
| `Tok` | [`Tok.scuzz`](../compiler-scuzz/src/Tok.scuzz) | Token stream; `Sym` keeps keyword/punct kinds as strings |
| `InterpPart` | [`InterpPart.scuzz`](../compiler-scuzz/src/InterpPart.scuzz) | `Lit(text: String)` / `Hole(expr: Expr)` |
| `ForBinder` | [`ForBinder.scuzz`](../compiler-scuzz/src/ForBinder.scuzz) | `Eq` / `Draw` with `value: Expr` |
| `Pattern` | [`Pattern.scuzz`](../compiler-scuzz/src/Pattern.scuzz) | `Wild` / `Adt(…, binds: List)`; unused pattern spans dropped |
| `MatchArm` | [`MatchArm.scuzz`](../compiler-scuzz/src/MatchArm.scuzz) | `Arm(pat: Pattern, body: Expr)` |
| `Span` | [`Span.scuzz`](../compiler-scuzz/src/Span.scuzz) | `Span(file, start, end)` Int payloads; folded into `Expr` cases |
| `EnumField` / `EnumCase` / `EnumDef` | [`EnumAst.scuzz`](../compiler-scuzz/src/EnumAst.scuzz) | Decl AST; `EnumField.ty: Type`; cases hold `List` of fields |
| `Param` / `Def` / `Prog` | [`Decl.scuzz`](../compiler-scuzz/src/Decl.scuzz) | Top-level program; `Param.ty` / `Def.ret` are `Type`; body/main are `Expr` |
| `Expr` | [`Expr.scuzz`](../compiler-scuzz/src/Expr.scuzz) | Recursive payload ADT; every case carries `span: Span` |
| `Type` / `TyRes` / `TyBind` | [`Type.scuzz`](../compiler-scuzz/src/Type.scuzz) | Replaces `"Int"` / `"Adt:…"` / `"IO[…]"` / `"Opaque:…"` string encodings; typecheck channel is `TyRes` |
| `Emit` / `EmitKind` / `EnvBind` / `CasePayload` | [`Emit.scuzz`](../compiler-scuzz/src/Emit.scuzz) | Codegen emit quad + env bindings + enum payload rows (`List` of `Type`) |

## Ordered residual

None — type strings and codegen emit/env encodings are done.

## Gate

Each family: Stage 0 already supports the needed payload shapes → rewrite `compiler-scuzz/` → `cargo test -p scuzz-compiler` as needed → `scripts/selfhost.sh` green.
