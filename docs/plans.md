# Current slice

- [x] Prove boxed Float equality differs from scalar Float equality.
- [x] Add a distinct Float box to the runtime.
- [x] Use Float boxes for list literals, cons, callbacks, fold seeds, and single-field enum payloads.
- [x] Use Float boxes for tuples, `List.fill`, and `List.append`.
- [x] Use Float boxes for list updates, zip-all defaults, maps, and sets.
- [x] Use Float boxes for `IO.pure`, `Ref.of`, `Stream.emit`, and `Stream.iterate`.
- [x] Use a Float box for `IO.fail` error values.
- [x] Use a Float box for the `Stream.unfold` seed.
- [ ] Audit the remaining typed boxing paths in IO, streams, and other scalar containers.
- [x] Run kernel, codegen, fuzz, and fixed-point checks.
- [x] Measure both compiler commands.
- [ ] Remove this plan when the typed boxing audit is complete.
