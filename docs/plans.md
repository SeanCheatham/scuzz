# Short-term plan

## Slice: Last-use release for `Map.getOrElse` default when the map is borrowed

An owned map drops its default after retain. A borrowed map does not, so `Map.getOrElse(m, k, "?")` leaks `"?"` on a hit.

- When the map is not owned, retain the result and drop an owned default only if that is safe, or drop the default when it is unused.
- Proof: compiler IR for `Map.getOrElse(id(Map.set(Map.empty(), "a", "1")), "a", "?")` shows `sz_release` of the default string.
- Out of scope: cycle collector, OS threads, device packaging.
