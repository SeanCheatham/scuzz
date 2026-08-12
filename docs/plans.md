# Short-term plan

## Next

**Generic enums/records (multi-param).** `enum Opt[T]:` / `record Pair[A, B](a: A, b: B)` + `Name[T, …]` type application; also lift the one-type-param limit on defs. Full monomorphization: typecheck with `Type.App`, elaborate instantiations onto construct/pattern nodes (constructor args, else expected type at def-ret/if/match-arm/call-arg), mono clones EnumDefs (`__gen_Name_T…`) and erases `App` before codegen. Codegen/runtime untouched. Stage 0 first (TDD), then mirror in `compiler-scuzz/`, new `examples/genum` in `selfhost.sh` stage_checks, gate green, then vision/gaps updates. Not OS threads, LSP, Windows, device Mobile, or GPU presenters until those proofs are in scope.
