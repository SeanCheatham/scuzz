# Short-term plan: thin monomorphized generics

**Goal.** One type parameter on a def, specialized per call-site type: `def id[T](x: T): T = x`, then `id(42)` / `id("hi")` → distinct mangled defs. Stage 0 + example; self-host mirror. No HKT, no trait bounds, no inferred type params on the def itself.

## Status

In progress — Stage 0 + `examples/generic` green (`7ok`); self-host next.

## Steps

1. **Surface (locked)** — `def name[T](…): R = …`; call sites supply concrete types via argument inference; monomorphize to `__gen_{name}_{Type}`.
2. **Stage 0** — Done (parse, typ unify, `monomorphize`, format).
3. **Example** — Done (`examples/generic`).
4. **Self-host** — Mirror `typeParams` + mono pass; smoke in `scripts/selfhost.sh`. Do not rewrite the compiler onto generics.
5. **Docs** — Update vision/gaps/guide; remove this file when done.

## Non-goals

- Multiple type params (unless free with the same machinery), trait bounds, `List[T]` / ADT type params, implicits, HKT, existential types
