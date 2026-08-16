# Short-term plan

## Slice: Last-use release for `Map.getOrElse` default temps

`Map.getOrElse` retains the result and drops an owned map. The default argument (`"?"`) stays allocated on a hit.

- Drop an owned default after `sz_map_get_or` (the result was retained when it aliased the default).
- Proof: compiler IR for `Map.getOrElse(Map.set(Map.empty(), "a", "1"), "a", "?")` shows `sz_release` of the default string.
- Out of scope: cycle collector, OS threads, device packaging.
