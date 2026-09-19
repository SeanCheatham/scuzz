# Growing Counter walkthrough

In progress. One program grows across six gated stages.

## Stages

1. Run — `@main` prints `inc(0)`. Press Run. See `1`.
2. View — mount a `View`. `+1` prints `inc(0)`. Press Run. See `Clicks: 0`.
3. Check — `@main` binds `p = ok(0)`. Press Check. See `true`.
4. Signal — live count. Tap Add one.
5. Search — Fuzz `hidden`. See `fail hidden 3`.
6. Cover — two scheduler worlds, coverage, mutant. `@main` prints the result.

Continue copies the next starter when the editor still matches the prior starter.

## Proof

`scuzz fuzz --iterations 0 examples/docs`. Runtime UI tests. Chromium, Firefox, and WebKit in `crates/embedder-web/test.cjs`.

Delete this file when the slice closes.
