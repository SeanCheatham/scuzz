# Short-term plan

## Next

**Language surface — `impl` methods that use the type parameter** — `impl Show for Opt` typechecks (`examples/trait`). Next: `impl Get for Opt` with `def getOrElse(default: T): T` so an impl method can mention `T`. `watch` still only rebuilds.
