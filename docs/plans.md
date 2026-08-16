# Short-term plan

## Slice: Last-use release for `List.setAt` lists

`List.setAt` builds a new list and does not drop the input list. Nested `List.setAt(["a", "b"], 0, "c")` stays allocated.

- Retain the result, then drop the owned input list (same last-use as `List.head` / `Map.getOrElse`).
- Proof: compiler IR for `List.join(List.setAt(["a", "b"], 0, "c"), ",")` shows `sz_release` of the input list after `sz_list_set_at`.
- Out of scope: cycle collector, OS threads, device packaging.
