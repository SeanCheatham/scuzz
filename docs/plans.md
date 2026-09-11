# Plan: typed agent session schema

One JSON schema for dump, inject, fuzz verdict, and coverage (`docs/gaps.md` thesis-critical 1). Text dump and script stay until the schema covers all four surfaces. Keys are snake_case, same as `scuzz check --message-format=json`.

## Slice 1: dump surface

A dump path that ends in `.json` writes the typed schema instead of the text format. Selection by suffix keeps the CLI, flags, and env (`SCUZZ_FUZZ_DUMP`, `SCUZZ_UI_DEBUG_DUMP`) unchanged. No new flags.

Document `v=1`:

- Envelope: `{"v":1,"kind":"dump",...}`.
- `signals`: `[{"id":N,"type":"int|str|list|value","name":"...","value":...}]`. Int is a number. Str is a string. List and value payloads stay opaque strings in v1.
- `views`: a11y preorder lines as an array of strings. A typed a11y tree is not v1.
- `taps`: `[{"i":N,"label":"...","x":F,"y":F,"w":F,"h":F}]`.
- `fields`: `[{"i":N,"target":B,"label":"...","value":"...","caret":N,"sel_start":N,"sel_end":N,"preedit":"..."}]`.
- `editors`: `[{"i":N,"target":B,"caret":N,"sel_start":N,"sel_end":N,"scroll_x":F,"scroll_y":F,"lines":N,"diags":[{"line":N,"severity":N}],"tokens":N,"inlays":N,"folds":N,"preedit":"...","value":"..."}]`.
- `splits`: `[{"i":N,"frac":N}]`. `overlays`: `[{"i":N,"top":B,"open":B}]`. `scrolls`: `[{"i":N,"label":"..."}]`.
- `last_hit` / `hover` / `last_secondary`: `{"x":F,"y":F,"desc":"..."}`. Absent when never seen.
- `session`: `{"runtime":"headless|desktop|mobile|web","width":N,"height":N,"title":"...","focus":"...","lifecycle":"resume|pause|stop","keyboard":B,"pumps":N}`. Same debug-dump-only rule as the text format.
- `heap`: `{"live_bytes":N,"live_count":N,"peak_bytes":N,"delta_bytes":N,"delta_count":N,"kinds":[{"kind":"raw","count":N,"bytes":N}]}`. Marks the delta baseline after write, same as the text format.
- `live`: `[{"kind":"raw","rc":N,"bytes":N}]`, capped at 32 rows.

### Status

- [x] `crates/runtime/src/signal.c`: `sz_signal_dump_json(FILE *)` next to `sz_signal_dump`.
- [x] `crates/runtime/src/ui.c`: `sz_ui_session_write_dump_json`; `.json` suffix routing in `sz_ui_session_write_dump`.
- [x] `crates/runtime/include/scuzz_ui.h`: declare the JSON writers.
- [x] `crates/runtime/tests/test_ui.c`: write a `.json` dump, parse with `sz_json_parse`, assert envelope, taps, fields, session, heap.
- [x] `scripts/ci.sh` `slice_ui`: counter headless with `--dump build/session.json`; validate with `python3` json.
- [x] `examples/manual/src/Topics.scuzz`: commands topic states the `.json` rule and the `v=1` sections.
- [x] `docs/vision.md`: schema paragraph names the landed dump surface and the `.json` rule.
- [x] `docs/gaps.md`: gap 1 residual = inject, fuzz verdict, coverage.

## Slice 2: inject surface

A script, record, or inject path that ends in `.json` uses the typed schema instead of the text verbs. Selection by suffix keeps the CLI, flags, and env (`SCUZZ_UI_SCRIPT`, `SCUZZ_UI_RECORD`, `SCUZZ_UI_INJECT`) unchanged. No new flags.

Document `v=1`:

- Envelope: `{"v":1,"kind":"inject","events":[...]}`.
- One object per verb. `op` names the verb. Indices are `i`. Payload strings are `value`. Points are `x` / `y` (`x1` / `y1` / `x2` / `y2` for drag). Keys: `{"op":"key","key":"Enter","text":"...","mods":["shift","ctrl","cmd","alt"],"repeat":B}`. `text` / `type` / `caret` / `select` / `backspace` take an optional `i` (absent = starred field). `scroll` takes optional `i` and `dy`. `secondary` takes `i` or `x` / `y`. `drive` takes `name` and typed `args`.
- Playback runs the same helpers and pump-after-event rule as the text verbs. A bad envelope or an unknown `op` panics, same as an unknown text directive.
- Record: a `.json` record path rewrites the whole document on each live OS event. Text keeps append-per-event.
- Watch inject: a `.json` inject document plays whole on change. It is not prefix-appended.

### Status

- [x] `crates/runtime/src/ui_script.c`: JSON playback (`sz_ui_script_play_json`), `.json` routing in `sz_ui_script_run_file`.
- [x] `crates/runtime/src/ui.c`: `.json` routing for watch inject and record; JSON record event writers.
- [x] `crates/runtime/src/ui_script.h` / `scuzz_ui.h`: declarations.
- [x] `crates/runtime/tests/test_ui.c`: play a JSON script (tap, text, key), replay a `.json` script file, record live events to a `.json` path and parse the result.
- [x] `scripts/ci.sh` `slice_ui`: counter headless with a `.json` script; assert the tap landed through the `.json` dump.
- [x] `examples/manual/src/Topics.scuzz`: commands topic states the `.json` script / record rule and the event ops.
- [x] `docs/vision.md`: schema paragraph names the landed inject surface.
- [x] `docs/gaps.md`: gap 1 residual = fuzz verdict, coverage.

## Slice 3: fuzz verdict surface

`scuzz fuzz` writes `build/fuzz/summary.json` next to `summary.toml`. The text summary stays until the schema covers every surface.

Document `v=1`:

- Envelope: `{"v":1,"kind":"fuzz",...}`.
- `fuzz`: `{"ok":B,"seed":N,"iterations":N,"search":N,"search_failures":N,"corpus":N,"repro":"..."}`. `repro` is absent when empty.
- `corpus`: `{"entries":N,"failures":N,"promoted":N}`.
- `classify`: `[{"name":"...","true":N,"false":N}]`. Absent when no labels.
- `mutate`: `{"killed":N,"survived":N,"inert":N,"ran":N,"sites":N,"oracles":B,"invalid":N,"score":F}`. `score` is absent when the denominator is zero.
- `coverage`: `{"total":N,"reached":N,"regions":[{"location":"...","reached":B}]}`.

### Status

- [x] `examples/compiler/src/Verify.scuzz`: `summaryJson` builders. `coverageRowsOf` shares one row computation between the text and JSON writers.
- [x] `examples/compiler/src/Drive.scuzz`: `fuzzWriteSummary` writes `summary.json` next to `summary.toml`.
- [x] `examples/cli/src/Main.scuzz`: oracle pins the JSON text and a `Json.parse` round-trip.
- [x] `scripts/ci-fuzz.sh`: validate `summary.json` with `python3` json on the bad-example and io campaigns.
- [x] `examples/manual/src/Topics.scuzz`: verify topic states the `summary.json` rule and the `v=1` sections.
- [x] `docs/vision.md`: schema paragraph names the landed fuzz verdict surface.
- [x] `docs/gaps.md`: gap 1 residual = typed a11y tree, typed list/value payloads. Gap 2 residual = branch coverage.

## Later slices (not started)

- Typed a11y tree and typed list/value signal payloads.
- Text dump, script, and summary removal once the schema covers every surface.
- Branch coverage (gaps.md thesis-critical 2).
