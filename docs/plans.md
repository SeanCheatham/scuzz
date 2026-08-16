# Short-term plan

## Slice: Last-use release for `Map.getOrElse` maps

`Map.contains` / `Map.set` drop an owned map. `Map.getOrElse` does not, because the result may alias a payload inside the map.

- Retain the result, then drop the owned map (same last-use as `List.head`).
- Proof: compiler IR for `Map.getOrElse(Map.set(Map.empty(), "a", "1"), "a", "?")` shows `sz_release` of the map after `sz_map_get_or`.
- Out of scope: cycle collector, OS threads, device packaging.
