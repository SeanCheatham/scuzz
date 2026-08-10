# ScalUI compiler (Stage 1 / 2)

ScalUI-written compiler + CLI. Stage 0 builds Stage 1; Stage 1 rebuilds itself (Stage 2). **Release ships Stage 2** (`scripts/package_release.sh`).

```bash
# Stage 0 hosts Stage 1
cargo run -p scalui -- build --full compiler-scalui

# Stage 1 builds hello
./compiler-scalui/build/scalui build examples/hello

# Dual-boot (Stage 1 → Stage 2 + IR fixpoint)
./scripts/selfhost.sh

# Package Stage-2 tarball (uses existing CLI when present; else Stage 0 once)
./scripts/package_release.sh
```

CLI: `scalui (build|run|test|check|fuzz|fmt|watch|new|package) [args]` (writes `project/build/`). `fuzz` lives here (not Stage 0): seeded `--iters`, bounded `--exhaust --depth N`, and `--replay`. Stage-0 Rust remains bootstrap only.
