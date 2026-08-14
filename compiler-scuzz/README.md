# Scuzz Lang compiler (Stage 1 / 2)

Scuzz Lang-written compiler + CLI. Stage 0 builds Stage 1; Stage 1 rebuilds itself (Stage 2). **Release ships Stage 2** (`scripts/package_release.sh`).

```bash
# Stage 0 hosts Stage 1
cargo run -p scuzz -- build --full compiler-scuzz

# Stage 1 builds hello
./compiler-scuzz/build/scuzz build examples/hello

# Dual-boot (Stage 1 → Stage 2 + IR fixpoint)
./scripts/selfhost.sh

# Package Stage-2 tarball (uses existing CLI when present; else Stage 0 once)
./scripts/package_release.sh
# Push a v* tag to publish GitHub Release assets; curl …/install.sh | sh to install
```

CLI: `scuzz (build|run|test|check|fuzz|mutate|fmt|watch|new|package) [args]` (writes `project/build/`). `scuzz --help` / `scuzz <command> --help` for flags and examples. `watch` rebuilds on change (not hot reload). `fuzz` and `mutate` live here (not Stage 0): seeded `--iters` (keep prefixes that hit new `Law.sometimes` names or a new Headless dump) / `--exhaust --depth N` / `--replay`, and residual-oracle `--limit N` plus per-mutant `--iters` probes. Stage-0 Rust remains bootstrap only.
