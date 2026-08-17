# Short-term plan

## Slice: Drop leftover Resource.use pack retains

`Net.serve` keeps the handler env in a pair until start. Unused serve drops leftover env retains. `Resource.use` still holds the resource and use env in a pack. Last-use of an unused use IO frees the pack and leaks those retains.

- Keep the use env in a pair (same shape as `Net.serve`) so last-use of the unused use IO drops leftover env retains. Steal on run so acquire still owns the resource.
- Proof: `test_io` unused `sz_lang_resource_use` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak.
