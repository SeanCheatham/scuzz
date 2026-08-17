# Short-term plan

## Slice: Drop leftover Sys.exec pack retains

`Net.httpGet` keeps the URL in a pair until dispatch. Unused get drops leftover URL retains. `Sys.exec` still uses a malloc `ExecSt` pack that holds the command string. Last-use `sz_free`s the pack and leaks the command.

- Keep the command in a pair (same shape as `Net.httpGet`) so last-use of the unused exec IO drops leftover command retains. Steal on run so the live path can still own the pipe and pid.
- Proof: `test_io` unused `sz_sys_exec` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak.
