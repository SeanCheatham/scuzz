# Short-term plan

## Next

**Resource on Stage 1/2.** Stage 0 already exposes `Resource.make` / `Resource.use` (String payload; bracket release on success and `IO` failure; `examples/resource`). Port parser / typer / codegen in `compiler-scuzz`, smoke the example through `scripts/selfhost.sh`, then close the gap. Kernel dialect only — no `Stream` or listen/serve in this slice.
