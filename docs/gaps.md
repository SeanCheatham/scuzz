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

**Status.** An OpenGL presenter is in (`SCUZZ_SKIA=gpu`): CPU `sk_sw` paint, GPU upload and readback behind `sk_capi`. Headless structural goldens match the CPU path. Impeller and Skia GPU raster stay deferred.

**Unproven.** A GPU rasterizer (Impeller or Skia GPU) behind `sk_capi` keeps identical structural dumps and tolerance-bounded pixels without a CPU paint pass.

**Proof.** `SCUZZ_SKIA=gpu` renders `examples/counter` with unchanged structural goldens. Pixel goldens match `sk_sw` when `--pixels` is on. Unused GPU stubs do not close the proof.

### 3. Dogfood IDE at editor scale

**Status.** Direction is locked in [`vision.md`](vision.md#tooling): a Scuzz `[ui]` app is the in-tree IDE. `scuzz ide` launches it. The compiler and `scuzz lsp` stay Rust. Prerequisites below are not closed. Do not start the IDE package yet.

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
- **OS IME candidate windows** — focused TextField caret uses measured advance (`sz_view_caret_rect`). Embedders do not yet place OS IME UI from it.
- **macOS in default CI** — `macos-smoke` runs on push/PR (runtime tests, compiler tests, hello). Full macOS packaging stays `workflow_dispatch`.
- **Web apps** — not a current target.
- **Fault injection and schedule exploration** — TestRuntime fakes can fail the Nth operation and drop stub responses. PCT-style schedule exploration bounds race depth for fiber interleavings. See [`vision.md`](vision.md#open-work).
- **Oracle idioms in `guide.md`** — model agreement, metamorphic relations, and concrete-fact properties ride the existing property surface. The guide does not yet document them.

### Dogfood IDE

A Scuzz `[ui]` package is the in-tree IDE. `scuzz ide` on the one CLI launches it with Desktop. Headless stays a peer. The app consumes `scuzz check`, `scuzz lsp`, `scuzz fmt`, `scuzz run`, and `scuzz fuzz`. It does not reimplement the compiler. Locks: [`vision.md`](vision.md#tooling).

Close every prerequisite in this section before that package and before `scuzz ide`. An implementation that invents a missing primitive at IDE time is a miss of this list. Sequence: input/editor and kits in parallel. Put Headless in every UI slice. Prove on `examples/editor` before the launcher. The close-lists below stay the bar.

#### 1. Input and embedder

Today Desktop (X11 and Cocoa) quit is window close. Live keys are `SZ_INPUT_KEY` (name, UTF-8 insert, Shift / Ctrl / Cmd / Alt). Headless `key <name>[+shift|+ctrl|+cmd|+alt] [text]` matches `record.script`. Insert is UTF-8 at the TextField or focused-editor caret. Backspace and Delete chop a UTF-8 code point at the caret, or delete a selection. Arrows, Home, and End move the caret. On `View.editor`, Enter inserts a newline, Tab inserts two spaces, and ArrowUp / ArrowDown move by line. Shift+arrows and Shift+Home/End extend the selection. A TAP / `xy` on a TextField or editor sets the caret from measured advance. Pointer drag extends the selection. Headless `caret <n>` sets the byte offset. `select <a> <c>` sets the selection. `type` / `text` / `backspace` stay agent sugar through `SZ_INPUT_TEXT_EDIT`. `SZ_INPUT_KEYBOARD` is mobile soft-keyboard show/hide, not a key. Mobile IME may still send `TEXT_EDIT`. Pointer MOVE with no button is hover. `SzInputEvent` carries `pointer_button` (0 hover, 1 primary, 3 secondary). Headless `hover x y` and `secondary N` / `secondary x y` match Desktop record. `View.tooltip` shows its message on hover. Session clipboard plus script `copy` / `cut` / `paste`; Headless `paste` is first-class. Desktop/Mobile sync the OS pasteboard in the embedder when present. OS IME candidate windows stay deferred (see above).

Close:

- Quit is window close (and Headless `quit`). A focused editor keeps `q` and Escape.
- One key event on Desktop, Mobile, Headless inject, and `record.script`. Payload: key, UTF-8 insert text, Shift / Ctrl / Cmd / Alt. Cover Enter, Tab, arrows, Home, End, PageUp, PageDown, Delete, Backspace.
- Backspace and arrows walk UTF-8 code points (or graphemes), not raw bytes.
- Insert is UTF-8, not Latin-1.
- Click-to-caret uses the pointer `(x, y)` on the editor. TextField TAP / `xy` sets the caret from measured advance. `View.editor` TAP / `xy` sets the caret from line and advance.
- Pointer hover without a button (hover docs, tooltips). Secondary click (context menu). Hover MOVE and button 3 are in. `View.tooltip` shows on hover. Context-menu chrome is not.
- Blessed clipboard: copy, cut, paste. Headless inject must drive paste. Do not make clipboard Desktop-only. Session clipboard plus `copy` / `cut` / `paste` are in. Desktop/Mobile sync the OS pasteboard in the embedder.
- Repeat keys and IME compose. IME UI placement can stay deferred if UTF-8 insert already works.

#### 2. Editor widget

Today `View.textField` is one line (`control_h`). It stores a caret byte offset and a selection range. Insert, Backspace, and Delete edit at the caret, or replace/delete the selection. Shift+arrows and pointer drag extend the selection. Arrows, Home, and End move it. `[fields]` dumps `caret=B sel=A:C` without shifting inject indices. Layout and paint copy the field value through a 256-byte stack buffer. `View.editor` is a multiline buffer on a `SignalStr`. It is not a TextField. Insert includes newline and a two-space soft-tab. Enter and Tab stay no-ops on a TextField. Editor layout, paint, caret, and a11y do not copy through a 256-byte stack. `[editor]` dumps one node: `N* caret=B sel=A:C sx=X sy=Y "escaped"` (newlines stay `\\n`; `sx`/`sy` are viewport pan). A11y dumps one `editor:editor` node. The editor pans to the caret. Long lines pan horizontally. A tall file pans vertically. Paint draws visible lines and visible columns on a monospace cell grid (`sk_font_measure_mono_string` / `sk_canvas_draw_mono_string` on every presenter). `View.fontSize` stays. The editor does not use the proportional UI font. `View.text` wraps at newlines for display, but it is not editable. `View.fontSize` / `View.textColor` wrap `View.text` only. A line of tokens as many Views is not an editor. `View.each` rebuilds every child. Collect of text fields caps at 64. `sk_sw` measure is monospace. The pinned Skia prebuilt is proportional for `View.text`.

Close:

- One multiline editor View (extend TextField or add a code editor). Headless is a peer. `View.editor` is in.
- Caret as a byte (or grapheme) offset. Click, arrows, Home, End. `View.editor` has this.
- Selection: Shift+arrows and pointer drag. Replace and delete the selection. TextField has this. `View.editor` has this.
- Insert and delete at the caret, including newline and tab / soft-tab. `View.editor` inserts newline on Enter and two spaces on Tab.
- Undo and redo.
- Scroll the viewport to the caret. Horizontal scroll for long lines. Vertical scroll for the file. The editor pans to the caret. Wheel over the editor pans Y.
- Paint and layout the full buffer. No 256-byte stack cap. `View.editor` is in.
- Monospace typeface on every presenter (`sk_sw`, Skia CPU, GPU present). `View.fontSize` stays. The editor does not use the proportional UI font. Mono cell measure/draw is in.
- Viewport virtualization. Do not layout one View per line for a large file. Do not dump one a11y node per token. A11y is one editor node. Paint draws visible lines and columns.
- Gutter: line numbers and diagnostic marks on the editor, not a second dumped tree.
- Headless dump: buffer text, caret, selection, and diagnostics. Keep `[fields]` / inject indices stable. Do not golden a token View forest. `[editor]` dumps buffer, caret, selection, and `sx`/`sy`. Diagnostics stay.
- Syntax highlight inside that widget (LSP semantic tokens or a lexer). Do not require a CustomPaint kit in Scuzz source for the first editor.

Folding ranges, inlay hints, and bracket match consume LSP data that already exists. They need the editor viewport first. Close them before a “real” IDE, after the caret and dump land.

#### 3. Kits the IDE process needs

Today there is no JSON kit. `Sys.exec` returns an exit code. It does not capture stdout or stderr. `Sys.spawn` returns a pid. It has no child stdin/stdout pipes. `Sys.exec` / `Sys.spawn` fail under TestRuntime. `Fs.list` is one directory of names (no file vs directory). There is no `Fs.delete`, `Fs.rename`, `Fs.exists`, `Fs.watch`, path join, dirname, or basename. `Fs.read` / `Fs.write` / `Fs.mkdirs` / `Fs.canonicalize` exist. `Sys.args` exists. `Clock` exists (poll is possible; there is no fs watch).

Close:

- JSON parse and stringify for `check --message-format=json` and, later, LSP JSON-RPC.
- Capture stdout and stderr from `Sys.exec` (or a sibling that returns `(code, stdout, stderr)`). The IDE prints `fmt` / `run` / `fuzz` output.
- Bidirectional stdio to a child before an in-process LSP client. Until then the IDE drives analyze through files: write the buffer, run `scuzz check --message-format=json` with stdout captured or redirected, parse JSON. That path matches the one editor protocol. Do not add a second typer.
- `Fs.list` reports file vs directory. Walk a tree (recursive list or a walk kit).
- `Fs.delete`, `Fs.rename`, `Fs.exists`. Rename must stay compatible with `workspace/willRenameFiles`.
- Path join, dirname, basename (blessed, not ad-hoc `Str` concat).
- File watch (`Fs.watch`) or a documented poll on `Clock` plus `Fs` that Headless can fake.
- TestRuntime story for check-from-IDE: a `*.scuzz_sim` overlay, or a fake that writes JSON onto the mem FS. Live `Sys.exec` must not be the only analyze path. Fuzz stays hermetic.

#### 4. Chrome around the editor

Today `examples/studio` shows pages, lists, and file load/save. `View.stack` / `View.positioned` exist. `View.tooltip` shows its message on hover. TextField and `View.editor` hold focus. Session title is `SzUiConfig.title` at start. There is no splitter, tab strip, overlay popup, or app-level key binding.

Close:

- Focus model: editor, file tree, diagnostics list, and popups. Keys go to the focused surface.
- File tree from `Fs.list` (files vs directories, expand/collapse).
- Open buffers: tabs, dirty flag, save / save-all via `Fs.write`.
- Split panes with a draggable splitter (Row/Column alone is not a resize handle).
- Overlay popups on `View.stack`: completion, hover, command palette, context menu. Key routing and dismiss. Headless inject opens and selects them.
- Diagnostics list from check JSON. Jump to file + caret.
- Find and replace in the buffer (needs selection).
- App-level chords once keys exist: save, find, go-to-definition, format, command palette. Same chords on Headless inject.
- Output panel for captured `run` / `fuzz` / `fmt` text.
- Window title that follows the open file and dirty state.
- Project root from `Sys.args` (what `scuzz ide <path>` passes).

In-app open-folder UI is enough. Native OS file dialogs, native menus, and multi-window stay later.

#### 5. Analyze consumption

`scuzz lsp` already serves hover, completion, definition, diagnostics, format, rename, semantic tokens, inlay hints, folding, code actions, and related methods. The IDE must consume them. It must not parse Scuzz in app code.

Close:

- Map LSP (or check JSON) line/character ranges onto editor offsets.
- Overlay unsaved buffers into check/lsp the same way `scuzz lsp` overlays `didChange`.
- Wire: diagnostics, go-to-definition, completion popup, hover overlay, format, rename, quickfix, semantic tokens into the editor widget.
- First slice may use `check` JSON only. Host `scuzz lsp` over stdio after JSON and child pipes close. Do not skip diagnostics.

#### 6. Headless and verification

Today inject verbs are `tap` / `xy` / `text` / `type` / `key` / `caret` / `select` / `copy` / `cut` / `paste` / `drag` / `hover` / `secondary` / `pump` / `scroll` / `backspace` / `dump` / `reload` / `quit` / `resetpeak`. `[fields]` dumps `caret=B sel=A:C`. `[editor]` dumps one node with buffer, caret, selection, and `sx`/`sy` when a `View.editor` is present. `[hover]` and `[last_secondary]` record pointer hover and button-3 hits. Fuzz composes taps, fields, scrolls, and drivers. A whole-file a11y dump of an editor is the wrong oracle.

Close:

- Inject verbs for caret, selection, key-with-modifiers, and paste. Record those verbs from Desktop. `key`, `caret`, `select`, `copy` / `cut` / `paste`, `drag`, `hover`, and `secondary` are in.
- Structural dump fields for buffer, caret, selection, and diagnostics. Properties talk to that surface (`Property.signalStr`, `Property.a11yHas`, or a dedicated editor observation). `[fields]` dumps `caret=B sel=A:C`. `[editor]` dumps buffer, caret, selection, and `sx`/`sy`. Diagnostics stay.
- Every new editor or chrome widget has a Headless path. No Desktop-only shortcut.
- IDE package uses in-source properties, drivers, goldens, and `scuzz fuzz`. TestRuntime fakes cover check/fs as in group 3.

#### 7. CLI and ship

Today `scuzz run examples/studio` opens Desktop when `[ui].default_runtime = "desktop"`. There is no `ide` subcommand. `install.sh` already ships the CLI plus SDK.

Close:

- `scuzz ide [path]` launches the bundled IDE package with Desktop. `--headless` stays.
- Bundle the package in the SDK. One CLI. No `scuzz-ide` binary. No editor widgets in the Rust CLI.
- Pass the project path through `Sys.args`.

#### Not a prerequisite for the first real IDE

Leave these until after `scuzz ide` exists. Do not block the list above on them:

- Multi-window, native menus, native file picker, drag-and-drop from the OS
- Multi-cursor, minimap, Git UI, debugger, plugin host
- Custom canvas kit from Scuzz source
- Windows desktop embedder, web target
- Flutter DevTools / VM patching (explicit non-goal)
