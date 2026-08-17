# Short-term plan

## Slice: Drop leftover Net.httpGet pack retains

`Fs.write` keeps path and contents in a pair. Unused delay drops leftover retains. `Net.httpGet` still uses a malloc `GetSt` pack that holds the URL and later socket state. Last-use `sz_free`s the pack and leaks the URL.

- Keep the URL in a pair (or retain it on the pack) so last-use of the unused get IO drops leftover URL retains. Steal on run so the live path can still own sockets.
- Proof: `test_io` unused `sz_net_http_get` then `sz_release` of the IO returns alloc stats to baseline.
- Out of scope: OS threads. Panic leak. Exec malloc packs.
