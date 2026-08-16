# Short-term plan

## Slice: Last-use release for `List.map` / `List.filter` closure packs

`List.map` / `List.filter` pack a closure env list (`sz_list_cons` of fn + env). That pack stays allocated after the call.

- Drop the owned closure pack after `sz_list_map` / `sz_list_filter`.
- Proof: compiler IR for `List.len(List.filter([1], x => true))` shows `sz_release` of the closure list after `sz_list_filter`.
- Out of scope: cycle collector, OS threads, device packaging.
