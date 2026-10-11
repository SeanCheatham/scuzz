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
Required PR integration and the current compiler fixed point remain open.
