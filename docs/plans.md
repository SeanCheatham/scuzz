# Short-term plan

## Slice: `Float` scalar type

Core value types come before mobile packaging (see [`gaps.md`](gaps.md)). `Float` is first because it is the cheapest: a scalar through the compiler with no memory-management implications.

- Add `Float` to the kernel: literals (`1.5`), arithmetic (`+ - * /`), comparisons, and `s"$x"` interpolation with one stable decimal format.
- Add `Float.fromInt(n): Float` and `Float.toInt(x): Int` (truncate).
- `scuzz fmt` and `scuzz check` cover the new syntax. Codegen lowers to LLVM `double`.
- Proof: `examples/kernel` exercises `Float`. `scuzz check` and `scuzz test` pass on `examples/`.
- Out of scope: `Map` / `Set` (they wait on reference counting), `Float` law generators, mobile CLI wiring.
