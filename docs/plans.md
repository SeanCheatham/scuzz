# Short-term plan

## Next

**Resource as a Scuzz builtin.** Runtime `sz_resource_make` / `sz_resource_use` already exists. Expose bracket acquire/release on the language surface (Stage 0, then `compiler-scuzz`): cleanup on success and on `IO` failure, a small example, TestRuntime coverage. Kernel dialect only — no `Stream` or listen/serve in this slice.
