# Current slice: module type identity

Status: declarations with repeated short names do not have separate module identities. Closed matches require complete coverage. Coverage proofs, native code-generation checks, and selected evaluator/native replay pass. Generated search and mutation checks pass. Read `HUMANS.md` and the language rules in `philosophy.md`.

## Outcome

Declarations in different modules have separate type identities. Constructor checking, field access, match coverage, evaluator values, and native tags use the same identity. File order does not change type resolution or execution.

## Work order

1. Resolve declared types by module. Preserve type parameters, imports, and local bindings. Reject an ambiguous name. Use one resolved representation.
2. Use the resolved identity in constructor checks, field lookup, match coverage, evaluator values, and native tags. Do not keep a second lookup by short name.
3. Prove both file orders, repeated short names, qualified constructors, generic payloads, and rejected cross-module values. Keep complete matches valid for each declaring module.
4. Update the language manual and design rules. Keep explicit excluded workload reports, production validation, and application model examples as the next guarantees in `vision.md`.

## Required validation

Add positive and negative compiler proofs. Rebuild the product CLI. Run formatting and type checks on changed packages. Run generated compiler checks, type-checker and code-generation proofs, and affected evaluator/native replay. Check the compiler and app packages. Run `git diff --check`.

## Limits

Do not add a theorem prover, a second type checker, or a new testing framework. Keep verification assertions and production validation separate. Do not weaken probe limits or remove a failing proof. Compiler idle cost work and editor feedback remain outside this slice.
