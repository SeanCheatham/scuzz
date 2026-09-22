# Scuzz Lang vision

Long-term planning arcs: open work and risks. Product intent, design locks, and language direction: [`philosophy.md`](philosophy.md). Keep/cut: [`compatibility.md`](compatibility.md). Ranked gaps: [`gaps.md`](gaps.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the next-step order changes.

## Open work

Next: make the language usable for general application development. Prioritize compiler correctness, then compile time, then standard kits. Prove each slice with examples. Specific application workflows do not define the scope.

The next slice is [`plans.md`](plans.md). `Type.eq` matches an unbound type parameter to any type. One check binds that parameter to one concrete type. Do not add a kit-call pin.

After that slice, re-time the two commands in [`gaps.md`](gaps.md) before another compile-time change. Then close the table-stakes gaps that block ordinary programs. Ranked list: [`gaps.md`](gaps.md).

The evaluator, `scuzz fuzz`, and the self-hosted CLI stay as locked in [`philosophy.md`](philosophy.md). A `[ui]` package runs compiled for every fuzz phase. Compiler sources use forms the newest `v*` bootstrap emits. A `for` pattern bind stays out of compiler sources until a release emits it.

Do not start FFI, a package registry, GPU raster, physical-device packaging, or `*.scuzz_tune` in this work.

## Risks

| Risk | Mitigation |
| --- | --- |
| An unbound type parameter matches every type | One concrete binding per parameter in one check. Existing pins stay. No new kit pin |
| Compile-time slices do not move the recorded times | Re-time the two commands in [`gaps.md`](gaps.md) before another show-and-parse change |
| An evaluator campaign stays as slow as a compiled campaign | The idle probe falls back to compiled. Speed stays open in [`gaps.md`](gaps.md) |
| Evaluator output differs from the emitted binary | Corpus replay runs compiled after an evaluator campaign. A difference fails the campaign |
| Self-hosting lags one release | Toolchain sources call builtins the newest `v*` release already emits |
| Mobile hardware and GPU raster stay unproven | Host and simulator proofs do not close them. See [`gaps.md`](gaps.md) |
| A `[ui]` package has no evaluator fuzz | `[ui]` fuzz stays compiled until a proof covers it |
| Reference counts miss Signal cycles and unmounted view lists | ASan corpus replay. The tree owns views |
| URLSession and Skia pixels have no fuzz home | `--live` replays host loopback. `--differential` compares structural dumps |
