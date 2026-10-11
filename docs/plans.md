# Result lists at input boundaries

## Behavior

A batch parser can return a list of Results.
A `for` binding must retain and use this list.
Reject a wildcard discard and an unused binding.
Apply the same rule to nested Result lists and IO payloads.
Keep ordinary lists valid.
Returning the list or passing it to a function stays valid.
This use check does not prove that a function handles each error.

## Work

- Extend the existing Result binding check for Result lists.
- Add source rejection properties and accepted-use controls.
- Add a live batch parser with an explicit failure policy.
- Check evaluator and native output against an independent expected result.
- Update the language manual and design limits.

## Required checks

- Reject the batch discard reproducer with a source diagnostic.
- Check wildcard, unused, nested, alias, bare generic, shadowed, and IO bindings.
- Preserve ordinary lists, function values, and explicit Result use.
- Run affected type-checker properties and compiler source checks.
- Rebuild the product CLI and check its fixed point.
- Run kernel and required integration checks.

## Status

The product CLI includes the Result-list check.
The batch discard reproducer has a source diagnostic at its binding.
Focused Result handling and source diagnostic checks pass.
Native and evaluator batch output matches the independent expected result.
The proof covers valid, invalid, out-of-range, and empty inputs.
Alias bindings use the Scuzz `name @ pattern` form.
The source check rejects a bound list of bare generic Results.
Compiler and type-checker source checks pass.
The type-checker executable includes the Result-list property and reports success.
All 32 type-checker replay entries and the required idle workload pass compiled.
The evaluator idle probe reaches its deadline and reports compiled fallback.
This compiled pass does not establish evaluator idle parity.
Type-checker generated and signal replay have equal timelines and claims on both engines.
All 37 code-generation replay entries and the required idle workload pass compiled.
Code-generation replay controls have equal timelines and claims on both engines.
The code-generation evaluator idle probe also reports its deadline and compiled fallback.
The native lexer proof passes 100000 identifiers with an 8 MiB stack.
The exact compiler fixed-point check passes.
Shared CLI and language-server project checks pass.
Live input validation, integer boundaries, and bounded native JSON checks pass.
Required PR integration remains open.
