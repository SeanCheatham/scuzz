# Gaps and unknowns

What is unproven or missing, ranked by how much it threatens the thesis in [`vision.md`](vision.md).

- **Unknowns** — claims not yet shown within our constraints. A bad outcome invalidates later work.
- **Known gaps** — settled design. Work is unfinished or deferred on purpose.

When a gap closes or its assessment changes, update this file. If direction changes, also update `vision.md`.

## Unknowns

### 1. Mobile on real devices

**Status.** iOS simulator proven: `scuzz package --target ios` runs `crates/embedder-mobile/shells/ios/build_sim.sh` and signs a `.app`. `examples/counter` mounts `UiRuntime.Mobile` in a booted sim. The iOS shell feeds typed insert and backspace into `SZ_INPUT_TEXT_EDIT`. Android emulator proven: `scuzz package --target android` packs a debug APK. SurfaceView taps become `SZ_INPUT_POINTER`. A hidden `EditText` maps insert and backspace to `SZ_INPUT_TEXT_EDIT`. Missing NDK or SDK fails with one install line. Real devices stay open (provisioning).

**Unproven.** JNI/ObjC embedding on hardware. Touch and soft-keyboard text input on hardware.

**Proof.** One example (counter) runs on one device or simulator with `scuzz package` plus the platform toolchain. iOS sim meets that bar. Android meets the emulator present + tap/TextField bar. Device runs stay open.

### 2. GPU presenters (Impeller / Skia GPU)

**Status.** An OpenGL presenter is in (`SCUZZ_SKIA=gpu`): CPU `sk_sw` paint, GPU upload and readback behind `sk_capi`. Headless structural goldens match the CPU path. `scuzz test --differential` compares structural dumps across `skia`, `sk_sw`, and `gpu` per host. Impeller and Skia GPU raster stay deferred.

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged structural goldens. Pixel goldens match `sk_sw` when `--pixels` is on. Unused GPU stubs do not close the proof.

### 3. Dogfood IDE at editor scale

**Status.** Direction is locked in [`vision.md`](vision.md#tooling): a Scuzz `[ui]` app is the in-tree IDE. `scuzz ide` launches the bundled package (`examples/editor` in a checkout; `SCUZZ_HOME/ide` from a release). The compiler and `scuzz lsp` stay Rust. `examples/editor` opens a project root from `Sys.args`, edits in `View.editor`, saves, runs `scuzz check --message-format=json`, hosts `scuzz lsp` over `Sys.spawn` pipes, and composes a nested file tree, extra tabs, find/replace, overlay stubs, output, and title. Fuzz overlays `analyze`, `lspCall`, `runProject`, and `fuzzProject` with canned JSON.

**Unproven.** An editor-scale Scuzz app stays inside Headless-as-peer, one input alphabet, the `pump` frame budget, and `scuzz fuzz` as the verification strategy. A second typer, Desktop-only keys, or a View-per-token tree that Headless cannot dump breaks those locks.

**Proof.** After the prerequisites close: one `[ui]` package opens a project, edits a buffer, shows `check` diagnostics, and saves. Headless goldens cover that path. `scuzz fuzz` drives caret, insert, and file-tree taps. Desktop and Headless share inject verbs.

## Known gaps

### Residuals

- **Concurrency** — cooperative fibers only. Later: OS threads, supervision trees.
- **Memory** — Last-use retain/release is locked in [`vision.md`](vision.md) GC. Values with no last-use stay allocated until panic sweep or process exit. No cycle collector.
- **LSP for external editors** — `scuzz lsp` wraps `check`. JSON diagnostics stay the single schema. Method catalog: [`vision.md`](vision.md#tooling). The dogfood IDE consumes that schema. It does not replace it. Prerequisites: [Dogfood IDE](#dogfood-ide).

### Dependency forms beyond `path`

Path deps only (`manifest.rs`). Git, versioned, and hosted artifacts are direction. There is no registry.

**Deferred (last on purpose).** Do not add ecosystem theater before there is an ecosystem. Revisit after path deps and file-as-module are the proven reuse story.

### Deferred by decision

- **Windows desktop embedder** — same session protocol as X11/Cocoa. Secondary platform.
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it. Desktop does not yet emit compose from XIM or Cocoa marked text.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, compiler tests, hello). Full macOS packaging stays `workflow_dispatch`.
- **Web apps** — not a current target.
- **Servers inside simulation** — in: TestRuntime uses a per-port virtual mailbox. Injected paths are ghost requests. Loopback `Net.httpGet` (`127.0.0.1` / `localhost` / `::1`) with no stub parks on a Deferred. Fake `Net.serve` parks on an empty mailbox. Write completes that Deferred. Stubs win over loopback. A Net fault fails before loopback. Persistent fake serve drains and stops when injects and the mailbox are empty. `Net.serveOnce` parks so `IO.both(serveOnce, httpGet)` works in either order. Proof: `examples/io` `Server.run` plus `scuzz test`. Scheduler ownership, not address, stays the determinism boundary.
- **Oracle idioms in `guide.md`** — Authors write `Timeline => Verdict` and drive oracles in `*.scuzz_verify`. Metamorphic swap and concrete-fact `.require` are in. English grammar, Given rows, and intent thunks are deferred with mining. They are not current work.
- **Timeline claim kernel** — in. Each fuzz run records an observation `State` per pump (signals, a11y, `[last_hit]`, drive records, effect log, fiber census, fault context). Session claims return `Verdict`; an invalid verdict carries the violating state index and evidence. The terminal boundary is typed: a run ends settled or quiesce-budget-tripped, and a tripped budget fails the run before claims judge. The timeline dump carries a version (`# timeline v=2`); loaders accept `v=1` and `v=2`. The compiler rejects other versions. Observation strings intern so equal dumps share one copy. Checkpoint flags mark retain points (`SCUZZ_TIMELINE_CHECKPOINT`). The dump writes `checkpoint=0|1` on each `---` line. `SCUZZ_TIMELINE_COMPACT=1` drops non-checkpoint states before dump and judge. Live replay restores observation from the nearest checkpoint. `Timeline.nearestCheckpoint` reads that index. Claims read through `tl_at`. An IO-only run pushes a terminal state when the pending effect log is not empty.
- **Observation-first `State`** — observation section first is in (signals, a11y, hits, drive records, effect log, fiber census, fault context). Heap deltas feed the terminal heap-baseline and acquire/release oracles. The effect log records op names, sizes, and hashes. It never records payloads. Fiber census records live / ready / parked / done. Fault context records kind, n, mode, and seed. Deterministic capture: creation-order ids, no pointers, no hash-map order. Schema version is 2.
- **Terminal-point universal oracles** — double-run same-seed determinism, heap block count and retain balance return to baseline after session teardown, acquire/release pairing, finalizer-on-cancel, no parked fibers at quiescence, and live/verify differential are in. Relation claims (`Timeline, Timeline => Verdict`) judge schedule-paired runs under `scuzz fuzz --relate`; the v1 timeline dump is the pair artifact. A `*.scuzz_sim` overlay is a declared live/verify delta.
- **Coverage axes** — breadth is in: `scuzz check` reports unclaimed defs, signals, and controls as info; the campaign reports reached states that vary in `State` fields no claim reads (`[coverage].unclaimed_varied` in `summary.toml`). Strength is in: mutation survivors that change observable behavior split into weak claims (claimed field) and missing claims (unclaimed field); inert mutants with bit-identical replayed timelines are unreported (`[mutate].survivors` kind and fields, `[mutate].inert`). Reachability stays `sometimes` plus unified unfired-trigger failure.
- **Stronger mutation operators** — in: boundary `where` mutations (Oracles mode negates or drops the residual bound), handler-swap on UI taps (Program mode rotates a tap handler body to a sibling handler), Signal-map identity (Program mode replaces the transform with the identity), and a mutation score in `summary.toml`. Mutants that do not compile count as killed.

### Dogfood IDE

A Scuzz `[ui]` package is the in-tree IDE. `scuzz ide` on the one CLI launches it with Desktop. Headless stays a peer. The app consumes `scuzz check`, `scuzz lsp`, `scuzz fmt`, `scuzz run`, and `scuzz fuzz`. It does not reimplement the compiler. Locks: [`vision.md`](vision.md#tooling).

`scuzz ide` launches the bundled package. Close-lists below stay the bar for that app. Headless is part of every UI slice. `examples/editor` is the proof and the SDK `ide/` tree.

#### 1. Input and embedder

Today Desktop (X11 and Cocoa) quit is window close. Live keys are `SZ_INPUT_KEY` (name, UTF-8 insert, Shift / Ctrl / Cmd / Alt, `key_repeat`). Headless `key <name>[+shift|+ctrl|+cmd|+alt|+repeat] [text]` matches `record.script`. Insert is UTF-8 at the TextField or focused-editor caret. A `+repeat` key uses the same session path as a discrete key: a held letter inserts again; Arrow / Backspace / Delete move or delete again. Desktop maps X11 auto-repeat and Cocoa `isARepeat` into that flag. Backspace and Delete chop a UTF-8 code point at the caret, or delete a selection. Arrows, Home, and End move the caret. On `View.editor`, Enter inserts a newline, Tab inserts two spaces, and ArrowUp / ArrowDown move by line. Shift+arrows and Shift+Home/End extend the selection. A TAP / `xy` on a TextField or editor sets the caret from measured advance. Pointer drag extends the selection. Headless `caret <n>` sets the byte offset. `select <a> <c>` sets the selection. Focused TextField and `View.editor` hold an IME **preedit** string. Headless `compose <text>` sets that preview (underlined; not in the committed buffer). `compose` with no text, or `commit`, inserts it at the caret (replaces a selection). `key Escape` cancels preedit. `[fields]` / `[editor]` dump `preedit="..."` when it is non-empty. `type` / `text` / `backspace` stay agent sugar through `SZ_INPUT_TEXT_EDIT`. `SZ_INPUT_KEYBOARD` is mobile soft-keyboard show/hide, not a key. Mobile IME may still send `TEXT_EDIT`. Pointer MOVE with no button is hover. `SzInputEvent` carries `pointer_button` (0 hover, 1 primary, 3 secondary). Headless `hover x y` and `secondary N` / `secondary x y` match Desktop record. `View.tooltip` shows its message on hover. `View.onSecondary` runs its handler on button 3. Session clipboard plus script `copy` / `cut` / `paste`; Headless `paste` is first-class. Desktop/Mobile sync the OS pasteboard in the embedder when present. OS IME candidate windows stay deferred (see above). Desktop X11 maps XIM preedit draw/done into `SZ_INPUT_COMPOSE`. Desktop Cocoa maps `NSTextInputClient` marked text and `insertText` into compose and text-edit events.

Close:

- OS IME candidate-window placement stays deferred.

#### 2. Editor widget

Today `View.textField` is one line (`control_h`). It stores a caret byte offset and a selection range. Insert, Backspace, and Delete edit at the caret, or replace/delete the selection. Shift+arrows and pointer drag extend the selection. Arrows, Home, and End move it. `[fields]` dumps `caret=B sel=A:C` without shifting inject indices, plus `preedit=` when IME compose is set. Layout and paint copy the field value through a 256-byte stack buffer. `View.editor` is a multiline buffer on a `SignalStr`. It is not a TextField. Insert includes newline and a two-space soft-tab. Enter and Tab stay no-ops on a TextField. Editor layout, paint, caret, and a11y do not copy through a 256-byte stack. `[editor]` dumps one node: `N* caret=B sel=A:C sx=X sy=Y lines=L diag=P:S tok=N inlay=N fold=N preedit="…" "escaped"` (newlines stay `\\n`; `sx`/`sy` are viewport pan; `diag` is omitted when empty; `tok` / `inlay` / `fold` / `preedit=` omit when zero or empty). A11y dumps one `editor:editor` node. The editor pans to the caret. Long lines pan horizontally. A tall file pans vertically. Paint draws visible lines and visible columns on a monospace cell grid (`sk_font_measure_mono_string` / `sk_canvas_draw_mono_string` on every presenter). A gutter paints line numbers, diagnostic marks, and fold marks. Ctrl/Cmd+Z undoes. Ctrl/Cmd+Y or Ctrl/Cmd+Shift+Z redoes. An in-widget lexer colors keywords, strings, comments, and numbers. LSP semantic tokens paint those spans when `Ui.setEditorTokens` has data. Empty tokens keep the lexer. Inlay hint labels paint in muted color at the cell. A fold mark in the gutter toggles closed interior lines. Bracket match highlights `()` / `[]` / `{}` near the caret. `View.fontSize` stays. The editor does not use the proportional UI font. `View.text` wraps at newlines for display, but it is not editable. `View.fontSize` / `View.textColor` wrap `View.text` only. A line of tokens as many Views is not an editor. `View.each` rebuilds every child. Collect of text fields caps at 64. `sk_sw` measure is monospace. The pinned Skia prebuilt is proportional for `View.text`.

#### 3. Kits the IDE process needs

Today `Json.parse` is `Result[Json]` and `Json.stringify` is `Result[String]` on blessed enum `Null|Bool|Int|Float|Str|Arr|Obj`. Query kits read that value: `Json.get` / `Json.keys` / `Json.arr` / `Json.at` / `Json.has` / `Json.pairs` / `Json.is*` / `Json.as*` / `Json.*Or` / `Json.getBool` / `Json.getInt` / `Json.getStr` / `Json.merge`. A miss or a wrong tag is an empty list. `*_or` / `get_*` use the default. Obj merge keeps the right value on a duplicate key. `Sys.exec` returns `(code, stdout, stderr)`. `Sys.spawn` returns a pid and attaches stdin/stdout pipes (`Sys.childWrite` / `Sys.childRead` / `Sys.childClose`). `Sys.exec` / `Sys.spawn` / child stdio fail under TestRuntime. `Fs.list` returns `(name, isDir)` entries. `Fs.exists`, `Fs.join`, `Fs.dirname`, `Fs.basename`, `Fs.delete`, `Fs.rename`, and `Fs.walk` exist. There is no `Fs.watch`. File change detection is a `Clock` plus `Fs` poll (`IO.sleep` / `Clock.monotonic` with `Fs.read` / `Fs.exists` / `Fs.list`). `examples/editor` forks a poll loop that reloads clean tabs when disk content changes and refreshes the tree. TestRuntime fakes Clock and Fs so Headless can drive that loop. `Fs.read` / `Fs.write` / `Fs.mkdirs` / `Fs.canonicalize` exist. `Sys.args` exists.

Live `Sys.exec` / `Sys.spawn` still fail under TestRuntime. Fuzz overlays `analyze`, `lspCall`, `runProject`, and `fuzzProject`. Do not add `Fs.watch` or an exec stub map.

#### 4. Chrome around the editor

Today `examples/editor` composes a nested file tree from `Fs.walk`, a tab list of open files with dirty marks, a wrapping toolbar, find/replace, completion/hover/palette overlays, a Context overlay on a tree-file button-3 click (Open / Delete), an output list that hides when empty, and `Ui.setTitle`. Nested tree rows indent. A dir tap expands or collapses children in place. The tree scrolls in the left pane. A tree, diagnostic, or def jump adds a tab when that path is not open, then selects it. Tab buttons switch. A close control drops a tab and keeps the last one. Save all writes every dirty tab. Run and Fuzz append captured `scuzz run` / `scuzz fuzz --iterations 0` text and set `showOut`. `examples/studio` shows pages, lists, and file load/save. `View.stack` / `View.positioned` exist. `View.tooltip` shows its message on hover. `View.onSecondary` runs an IO/unit handler on button 3. TextField and `View.editor` hold focus. A button tap clears that focus. Tree and diagnostic rows wrap in `View.focusGroup`. A tap on a row focuses that list (`[session]` `focus=button:<label>`). ArrowUp / ArrowDown move among sibling rows when no overlay is open. Enter / Space activate the focused row. An open overlay still takes keys. `View.split` is a draggable 0–100 pane. `View.overlay` fills the parent when open; Escape and a backdrop tap dismiss it. Keys go to the open overlay. Ctrl/Cmd+S / F / Shift+F / K / P fire labeled toolbar buttons. Ctrl/Cmd+Shift+P opens Palette. A diagnostic row encodes `line:col|file|message`. A tap opens that file when `file` is set, then jumps the caret. Def opens a definition uri when set. Fix applies the first `newText` from a code action and still appends the title. `[session]` dumps `title=` and `focus=`. `[splits]` / `[overlays]` dump those widgets.

In-app open-folder UI is enough. Native OS file dialogs, native menus, and multi-window stay later.

#### 5. Analyze consumption

`scuzz lsp` already serves hover, completion, definition, diagnostics, format, rename, semantic tokens, inlay hints, folding, code actions, and related methods. The IDE consumes them. It does not parse Scuzz in app code. Hover and Check decode `textDocument/semanticTokens/full`, `textDocument/inlayHint`, and `textDocument/foldingRange` JSON in `Lsp.scuzz` and pass lists into `Ui.setEditorTokens` / `Ui.setEditorInlays` / `Ui.setEditorFolds`. The editor paints those spans. `[editor]` dumps `tok=` / `inlay=` / `fold=` counts.

#### 6. Headless and verification

Today inject verbs are `tap` / `xy` / `text` / `type` / `key` / `compose` / `commit` / `caret` / `select` / `copy` / `cut` / `paste` / `drag` / `hover` / `secondary` / `pump` / `scroll` / `backspace` / `dump` / `reload` / `quit` / `resetpeak`. `[fields]` dumps `caret=B sel=A:C` and `preedit=` when compose is set. `[editor]` dumps one node with buffer, caret, selection, `sx`/`sy`, `lines`, `diag`, `tok`, `inlay`, `fold`, and `preedit=` when a `View.editor` is present. `[hover]` and `[last_secondary]` record pointer hover and button-3 hits. Fuzz composes taps, fields, scrolls, and drivers. A whole-file a11y dump of an editor is the wrong oracle.

Close:

- Every new editor or chrome widget has a Headless path. No Desktop-only shortcut.

#### 7. CLI and ship

Today `scuzz ide [path]` launches the bundled `[ui]` package with Desktop. `--headless` stays. `install.sh` ships the CLI plus SDK (`SCUZZ_HOME/ide`). `scuzz run examples/studio` opens Desktop when `[ui].default_runtime = "desktop"`.

#### Not a prerequisite for the first real IDE

These stay later. They do not block the list above:

- Multi-window, native menus, native file picker, drag-and-drop from the OS
- Multi-cursor, minimap, Git UI, debugger, plugin host
- Custom canvas kit from Scuzz source
- Windows desktop embedder, web target
- Flutter DevTools / VM patching (explicit non-goal)
