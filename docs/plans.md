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

## Later slices (not started)

- Inject: JSON script form of the `*.script` verbs through the same schema.
- Fuzz verdict: `build/fuzz/summary` in the schema. Text `summary.toml` stays until then.
- Coverage: source-region coverage output in the schema (gaps.md thesis-critical 2).
- Typed a11y tree and typed list/value signal payloads.
