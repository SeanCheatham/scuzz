# Live Result use

## Behavior

A Result binding needs a use that remains in live code.
A `.require` receiver remains in live code.
Its label and predicate do not remain in live code.
Do not count uses in those erased arguments.
Apply the same rule to Result lists and selected Result fields.
Keep ordinary matching, returning, and function calls valid.
A function call does not prove that its receiver handles the error.

## Work

- Make the Result use check follow live expression use.
- State the live-use requirement in the diagnostic and manual.
- Preserve shadowing, aliases, and nested binding rules.

## Required checks

- Reject scalar, list, and IO Results used only in erased arguments.
- Reject erased labels and nested predicates.
- Preserve live matching, returning, forwarding, and receiver use.
- Check source diagnostics through check, build, and eval.
- Rebuild the CLI and check the application input proofs.
- Run the affected compiler and integration checks.

## Status

Result checks use live variable analysis.
Code emission uses source variable analysis.
The rebuilt CLI rejects the original input-guard reproducer.
Focused check, build, and eval diagnostics pass.
Native and evaluator receiver-use controls pass.
The new type-checker oracle executes and passes.
The product manual states the live-use requirement.
All 34 required type-checker workloads pass.
The evaluator idle deadline requires compiled fallback.
Generated type-checker timelines and claims match on both engines.
All 38 required code-generation workloads pass.
The code-generation evaluator idle probe requires compiled fallback.
Generated code, module, and View timelines match on both engines.
The native lexer and probe startup controls pass.
The compiler fixed-point check passes.
Shared CLI and language-server project checks pass.
The remaining application and integration checks remain open.
