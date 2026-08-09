# Stage 1 ScalUI compiler

ScalUI-written compiler + CLI. Built by Stage 0, then rebuilds itself (Stage 2).

```bash
# Stage 0 hosts Stage 1
cargo run -p scalui -- build --full compiler-scalui

# Stage 1 builds hello
./compiler-scalui/build/scalui build examples/hello

# Dual-boot (Stage 1 → Stage 2; raises stack for recursive emit)
./scripts/selfhost.sh
```

Stage-1 CLI: `scalui (build|run) [project]` (writes `project/build/`). Stage 0 Rust CLI remains a CI canary.
