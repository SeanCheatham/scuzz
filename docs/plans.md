# Short-term plan

## Slice: Fake `Sys.alive` / `Sys.kill` under TestRuntime

`Sys.exec` and `Sys.spawn` fail under TestRuntime. `Sys.getenv` is sealed. `Sys.alive` and `Sys.kill` still call the host.

- Under `SCUZZ_TESTRT=1`, `Sys.alive` / `Sys.kill` use a fake process table (or fail like exec/spawn). Live code keeps `waitpid` / `kill`.
- Proof: `examples/io` or `crates/runtime` tests do not touch host pids. A fake pid reports alive then dead after kill.
- Out of scope: OS threads, Impeller, device packaging.
