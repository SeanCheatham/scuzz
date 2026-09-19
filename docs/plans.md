# Evaluator slice 6: browser

Locks: [`philosophy.md`](philosophy.md#evaluator). Arc: [`vision.md`](vision.md#evaluator-arc). Steps run in order. Each step ends with a proof and a commit. Slice 7 (guided tutorial) adds the reduction trace to the step 2 entry; step 2 does not carry a placeholder for it.

## Step 1: UI kits at `Value`

Status: done.

`Value` gains `VView(kind, args)`, `VSigInt`, `VSigStr`, and `VSig(Signal[Value])`. Every `View.*` call, in the kit table or a variadic form, evaluates to `VView` with its evaluated args. `Signal.*` cases build native signals; `Signal.map` calls back into `applyValue` the way `Stream.map` does. `Icon`, `Color`, and `Theme` are native calls. `Ui.run` applies its callback and stores the `VView` in the host `Ref` from `Eval.withHost`; without a host it fails loud. `Ui.setTitle` is a no-op. `Eval.excludedKits()` keeps `Ui.setEditor*`, `Ui.editorCaret`, `Property.signal*`, `Property.a11yHas`, and `Fuzz.`. The compiler package still links without Skia.

Proof: `examples/codegen` `evKitsCovered` probes every UI row; `evCounterView` evaluates a counter main to `Ui.run`, applies the button closure from the description, and reads `Count: 1` through the label signal (`eval-ui-ok` in `scripts/ci.sh codegen`).

## Step 2: Try it in Docs, headless

Status: in progress.

`examples/docs` depends on `examples/compiler`. `Mount.scuzz` in Docs walks a `VView` description into a native `View`: closures become taps through `Eval.applyValue`, `VSigInt` and `VSigStr` pass through, `VSig` maps at the boundary. Boxes (`column`, `row`, `stack`, `wrap`, `grid`, `breadcrumb`) take up to eight children per level; `column` beyond that nests, others fail loud. A "Try it" topic page holds a `View.editor` bound to a source signal, a diagnostics text, and the mounted view. `Eval.tryIt(src)` checks one module source and returns diagnostics or the `VView` of its `@main` `Ui.run` callback. An evaluator stop renders as a diagnostic. The mounted view is replaced on every evaluation.

Proof: Headless claims in `examples/docs` type the counter source into the editor, tap `+1`, and read the label (`scuzz fuzz --iterations 0 examples/docs`).

## Step 3: Try it in Chromium

Status: pending.

`scuzz package --target web examples/docs` ships the compiler front to wasm32. `crates/embedder-web/test.cjs` types the counter and clicks `+1`.

Proof: the `web` CI slice.
