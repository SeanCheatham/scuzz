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

Stage-1 CLI: `scalui (build|run|test|check|fuzz|fmt|watch|new|package) [args]` (writes `project/build/`). `fuzz` lives in Stage 1 (not Stage 0). Stage-0 Rust remains the bootstrap host.
