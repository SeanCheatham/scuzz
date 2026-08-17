# Short-term plan

## Slice: Keep Sys.exec live pack RC

`Net.serve` live pack is RC so HANDLE last-use drops the pack. `Sys.exec` still mallocs `ExecSt`. `exec_free` frees it while HANDLE still holds the pointer. Freed memory can look like RC.

- Keep `ExecSt` RC (same shape as the live Net.serve pack) so HANDLE last-use drops the pack. Do not `sz_free` in `exec_free`.
- Proof: `test_io` live leftover unused exec stays at baseline. A started exec that fails under TestRuntime still returns to alloc baseline.
- Out of scope: OS threads. Panic leak.
