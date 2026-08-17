# Short-term plan

## Slice: Drop leftover Net.serve pack retains

`Sys.exec` keeps the command in a pair until start. Unused exec drops leftover command retains. `Net.serve` still uses a malloc `ServeSt` pack that holds the handler env. Last-use `sz_free`s the pack and leaks that env.

- Keep the handler env in a pair (same shape as `Net.httpGet`) so last-use of the unused serve IO drops leftover env retains. Steal on run so the live path can still own listen sockets.
- Proof: `test_io` unused `sz_net_serve` / `sz_net_serve_once` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak.
