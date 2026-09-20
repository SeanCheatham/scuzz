# Growing Counter walkthrough

In progress. One program grows across eight gated stages.

## Stages

1. Intro — short language and tooling overview. Continue is ready.
2. Run — `@main` prints `inc(0)`. Press Run. See `1`.
3. View — mount a `View`. Press Run. Tap `+1`. The label stays `Clicks: 0`.
4. Check — nested tabs show `Main.scuzz` and `count.scuzz_verify`. The verify tab opens. Press Check. `oracle incAdds` returns true. See `true`.
5. State — mount the Counter `Signal`. Press Run. Tap `+1`. Continue waits for that tap.
6. Search — Fuzz `oracle hidden` in `count.scuzz_verify`. See `fail hidden 3`.
7. Cover — two scheduler worlds and painted coverage of `inc`. Continue is ready.
8. Mutation — live source, mutant source, and the diff. A mutant flips `+` in `inc`. `incAdds` rejects it.

Continue copies the next starter when the live editor and the verify editor still match the prior starters. Continue stays above the stage body. The app bar title shows `Name n/8`. Cover and Mutation construct viz when those stages open.

## Proof

`scuzz fuzz --iterations 0 examples/docs`. Runtime UI tests. Chromium, Firefox, and WebKit in `crates/embedder-web/test.cjs`.

Delete this file when the slice closes.
