# Short-term plan

## Slice: Keep Net.httpGet live pack RC

`Sys.exec` live pack is RC so HANDLE last-use drops the pack. `Net.httpGet` still mallocs `GetSt`. `get_free` frees it while HANDLE still holds the pointer. Freed memory can look like RC.

- Keep `GetSt` RC (same shape as the live Sys.exec pack) so HANDLE last-use drops the pack. Do not `sz_free` in `get_free`.
- Proof: `test_io` unused `sz_net_http_get` stays at baseline. A stub get under TestRuntime returns to alloc baseline after the result is dropped.
- Out of scope: OS threads. Panic leak.
