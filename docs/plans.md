# Short-term plan

## Slice: Keep Net.serve live pack RC

`Resource.use` keeps the resource and use env in a pair until start. Unused use drops leftover env retains. The live use pack is RC so HANDLE last-use drops the pack. `Net.serve` still mallocs `ServeSt`. `serve_free` frees it while HANDLE still holds the pointer. Freed memory can look like RC.

- Keep `ServeSt` RC (same shape as the live Resource.use pack) so HANDLE last-use drops the pack. Do not `sz_free` in `serve_free`.
- Proof: `test_io` live `sz_net_serve_once` still returns to alloc baseline. Unused serve stays at baseline.
- Out of scope: OS threads. Panic leak.
