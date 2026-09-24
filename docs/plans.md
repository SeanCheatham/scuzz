# Current slice

- [x] Prove boxed Float equality differs from scalar Float equality.
- [x] Add a distinct Float box to the runtime.
- [x] Use Float boxes for list literals, cons, callbacks, fold seeds, and single-field enum payloads.
- [x] Use Float boxes for tuples, `List.fill`, and `List.append`.
- [ ] Audit the remaining typed boxing paths in maps, sets, IO, streams, and list kits.
- [x] Run kernel, codegen, fuzz, and fixed-point checks.
- [x] Measure both compiler commands.
- [ ] Remove this plan when the typed boxing audit is complete.
