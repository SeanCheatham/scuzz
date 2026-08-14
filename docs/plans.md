# Short-term plan

## Next

**Language surface — methods on generic enums** — `impl Show for Box` typechecks (`examples/trait`). Next: one method on `enum Opt[T]` so `o.getOrElse(0)` typechecks without a free `def`. `watch` still only rebuilds.
