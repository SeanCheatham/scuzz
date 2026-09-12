#include "ui_script.h"

#include "scuzz_rt.h"
#include "rt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sz_ui_collect_buttons(SzUiSession *session, SzView **buttons, int cap) {
  SzView *r = sz_ui_session_root(session);
  if (!r || !buttons || cap <= 0)
    return 0;
  return sz_view_collect_tap_targets(r, buttons, cap);
}

int sz_ui_collect_scrolls(SzUiSession *session, SzView **scrolls, int cap) {
  SzView *r = sz_ui_session_root(session);
  if (!r || !scrolls || cap <= 0)
    return 0;
  return sz_view_collect_scrolls(r, scrolls, cap);
}

static void script_scroll(SzUiSession *session, int index, float dy) {
  SzView *scrolls[64];
  int count = sz_ui_collect_scrolls(session, scrolls, 64);
  int n = index < 0 ? 0 : index;
  if (count <= 0 || n >= count) {
    if (index < 0)
      fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
    else
      fprintf(stderr, "scuzz: script scroll %d skipped (%d scrolls)\n", n, count);
    return;
  }
  if (!sz_ui_scroll_index(session, n, dy))
    fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
}

static int script_focus_field(SzUiSession *session, int index) {
  SzView *root = session ? sz_ui_session_root(session) : NULL;
  if (index >= 0) {
    if (root && sz_view_focus_text_field_at(root, index))
      return 1;
    fprintf(stderr, "scuzz: script field %d skipped\n", index);
    return 0;
  }
  if (root && sz_view_focus_edit_target(root))
    return 1;
  fprintf(stderr, "scuzz: script skipped (no text field)\n");
  return 0;
}

static void script_backspace(SzUiSession *session, int index, int n) {
  SzInputEvent ev;
  if (n < 1)
    n = 1;
  if (!script_focus_field(session, index))
    return;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = "";
  while (n-- > 0) {
    if (!sz_ui_inject_sync(session, &ev)) {
      fprintf(stderr, "scuzz: script backspace skipped (no text field)\n");
      return;
    }
  }
}

static void script_type(SzUiSession *session, int index, const char *text) {
  SzInputEvent ev;
  if (!text || !text[0])
    return;
  if (!script_focus_field(session, index))
    return;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = text;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script type skipped (no text field)\n");
}

static void script_key(SzUiSession *session, const char *name, const char *text,
                       int mods, int repeat) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_KEY;
  ev.key = name ? name : "";
  ev.text = text ? text : "";
  ev.key_mods = mods;
  ev.key_repeat = repeat ? 1 : 0;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script key skipped\n");
}

static void script_compose(SzUiSession *session, const char *text) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_COMPOSE;
  ev.text = text ? text : "";
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script compose skipped\n");
}

static int script_eq_mod(const char *s, const char *mod) {
  size_t i;
  if (!s || !mod)
    return 0;
  for (i = 0; s[i] && mod[i]; i++) {
    char a = s[i];
    char b = mod[i];
    if (a >= 'A' && a <= 'Z')
      a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
      b = (char)(b - 'A' + 'a');
    if (a != b)
      return 0;
  }
  return s[i] == '\0' && mod[i] == '\0';
}

static void script_drag(SzUiSession *session, float x1, float y1, float x2,
                       float y2) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.pointer_button = 1;
  ev.x = x1;
  ev.y = y1;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script drag inject failed");
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.x = x2;
  ev.y = y2;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script drag inject failed");
  ev.pointer_phase = SZ_POINTER_UP;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script drag inject failed");
}

void sz_ui_scripted_button_tap(SzUiSession *session, int prefer_upper) {
  SzInputEvent tap;
  SzView *hit_btn = NULL;
  SzView *buttons[64];
  int n_buttons;
  float tx = 40.f, ty = 60.f;
  const char *tap_n_env = getenv("SCUZZ_UI_TAP_N");
  int tap_n = (tap_n_env && tap_n_env[0]) ? atoi(tap_n_env) : -1;

  n_buttons = sz_ui_collect_buttons(session, buttons, 64);

  if (tap_n >= 0) {
    if (tap_n >= n_buttons)
      sz_panic("Ui.run: SCUZZ_UI_TAP_N out of range");
    hit_btn = buttons[tap_n];
  } else if (prefer_upper && n_buttons > 0) {
    int bi;
    hit_btn = buttons[0];
    for (bi = 1; bi < n_buttons; bi++) {
      if (sz_view_frame(buttons[bi]).y < sz_view_frame(hit_btn).y)
        hit_btn = buttons[bi];
    }
  } else if (n_buttons > 0) {
    hit_btn = buttons[0];
  }

  if (!hit_btn)
    sz_panic("Ui.run: button not found for SCUZZ_UI_TAP");
  {
    SzRect fr = sz_view_frame(hit_btn);
    tx = fr.x + fr.w * 0.5f;
    ty = fr.y + fr.h * 0.5f;
  }
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = tx;
  tap.y = ty;
  if (!sz_ui_inject_sync(session, &tap) || !sz_ui_pump_sync(session))
    sz_panic("Ui.run tap/pump failed");
}

/* --- SCUZZ_UI_SCRIPT playback (fuzz / replay) ---------------------------- */
/* Shared event helpers. The inject schema below is the only script format.
 * Pump runs after every event except quit. */

static void script_tap(SzUiSession *session, int n) {
  SzView *buttons[64];
  int count = sz_ui_collect_buttons(session, buttons, 64);
  if (n < 0 || n >= count) {
    fprintf(stderr, "scuzz: script tap %d skipped (%d tap targets)\n", n, count);
    return;
  }
  if (!sz_ui_session_activate_view(session, buttons[n]))
    sz_panic("Ui.run: script tap activate failed");
}

static void script_xy(SzUiSession *session, float x, float y) {
  SzInputEvent tap;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SZ_INPUT_TAP;
  tap.x = x;
  tap.y = y;
  if (!sz_ui_inject_sync(session, &tap))
    sz_panic("Ui.run: script xy inject failed");
}

static void script_hover(SzUiSession *session, float x, float y) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_MOVE;
  ev.pointer_button = 0;
  ev.x = x;
  ev.y = y;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script hover inject failed");
}

static void script_secondary_xy(SzUiSession *session, float x, float y) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = SZ_POINTER_DOWN;
  ev.pointer_button = 3;
  ev.x = x;
  ev.y = y;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script secondary inject failed");
  ev.pointer_phase = SZ_POINTER_UP;
  if (!sz_ui_inject_sync(session, &ev))
    sz_panic("Ui.run: script secondary inject failed");
}

static void script_secondary_n(SzUiSession *session, int n) {
  SzView *buttons[64];
  SzRect fr;
  int count = sz_ui_collect_buttons(session, buttons, 64);
  if (n < 0 || n >= count) {
    fprintf(stderr, "scuzz: script secondary %d skipped (%d tap targets)\n", n,
            count);
    return;
  }
  fr = sz_view_frame(buttons[n]);
  script_secondary_xy(session, fr.x + fr.w * 0.5f, fr.y + fr.h * 0.5f);
}

/* Pump tail after every script event except quit. */
static void script_after_event(SzUiSession *session) {
  if (!sz_ui_session_alive(session))
    return;
  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run: script pump failed");
}

/* --- typed session schema v=1 (JSON inject) ------------------------------ */
/* Document: {"v":1,"kind":"inject","events":[...]}. One object per event:
     {"op":"tap","i":N}        activate the nth tap target in a11y preorder; missing target is a no-op
     {"op":"xy","x":X,"y":Y}  inject TAP at logical point; miss does not panic
     {"op":"text","value":S}  replace the [fields] starred TextField with S; no field is a no-op
     {"op":"text","i":N,"value":S}  replace dump-index N (a11y order)
     {"op":"type","value":S}  insert S at the caret on the starred TextField; empty is a no-op
     {"op":"type","i":N,"value":S}  insert at the caret on dump-index N
     {"op":"key","key":K,"text":T,"mods":[...],"repeat":B}
                        named key on the starred TextField or focused editor
                        (`Enter`, `Backspace`, `ArrowLeft`, `a`);
                        optional UTF-8 insert text; arrows / Home / End / Delete use the caret;
                        Enter / Tab insert newline / two spaces on an editor;
                        PageUp / PageDown move by the viewport on an editor;
                        Ctrl/Cmd+Z undoes, Ctrl/Cmd+Y or Ctrl/Cmd+Shift+Z redoes;
                        mods are `shift` / `ctrl` / `cmd` / `alt`;
                        repeat is a held-key auto-repeat (same insert / move / delete)
     {"op":"compose","value":S}  set IME preedit on the starred field or focused editor
                     (underlined preview; not in the committed buffer)
     {"op":"commit"}   commit preedit into the buffer at the caret (replaces a selection)
     key Escape      cancel preedit when compose is active; else dismiss an overlay
     {"op":"caret","offset":N}  set the starred TextField or focused-editor caret to byte offset N
     {"op":"caret","i":I,"offset":N}  set dump-index I caret to byte offset N
     {"op":"select","start":A,"end":C}  set the starred TextField or focused-editor selection `[A, C)` (caret at C)
     {"op":"select","i":I,"start":A,"end":C}  set dump-index I selection
     {"op":"copy"}    copy the starred-field or focused-editor selection into the session clipboard
     {"op":"cut"}     copy then delete the selection
     {"op":"paste"}   insert the session clipboard (Headless) or OS pasteboard (Desktop/Mobile)
     {"op":"paste","value":S}  set the session clipboard to S then paste
     {"op":"drag","x1":..,"y1":..,"x2":..,"y2":..}  pointer drag (TextField / editor selection)
     {"op":"hover","x":X,"y":Y}  pointer MOVE with no button (hover); shows View.tooltip
     {"op":"secondary","i":N}  button-3 click on tap-dump index N; does not fire the primary tap; runs View.onSecondary
     {"op":"secondary","x":X,"y":Y}  button-3 click at a logical point
     {"op":"pump","k":K}  pump K extra frames
     {"op":"scroll","dy":D}  pan the first Scroll on its axis (positive = content up or left); no scroll is a no-op
     {"op":"scroll","i":N,"dy":D}  pan dump-index N ([scrolls] scan order)
     {"op":"backspace","count":K}  chop K UTF-8 code points before the caret on the [fields] starred TextField (default 1); no field is a no-op
     {"op":"backspace","i":N,"count":K}  chop K code points before the caret on dump-index N
     {"op":"dump"}    rewrite the live debug dump now (includes heap and live rows); no dump path is a no-op
     {"op":"reload"}  rebuild the View factory now; missing factory is a no-op
     {"op":"quit"}    stop the live session; remaining events do not run
     {"op":"resetpeak"}  set peak_bytes to live and mark delta; next dump reports growth from here
     {"op":"drive","name":N,"args":[...]}  run a verify-graph driver (Int/String/Bool args)
   Pump runs after every event except quit. A bad envelope or an unknown op
   panics. */

/* A script / record / inject path must end in `.json`. */
static int script_path_is_json(const char *path) {
  size_t n = path ? strlen(path) : 0;
  return n >= 5 && strcmp(path + n - 5, ".json") == 0;
}

static int jev_mods(SzAdt *ev) {
  SzAdt *arr = sz_jev_key(ev, "mods");
  SzList *xs, *p;
  int mods = 0;
  if (!arr || !sz_json_is_arr(arr))
    return 0;
  xs = sz_json_arr(arr);
  for (p = xs; p && !sz_list_is_empty(p); p = sz_list_tail(p)) {
    SzAdt *m = (SzAdt *)sz_list_head(p);
    const char *s;
    if (!m || !sz_json_is_str(m))
      continue;
    s = sz_string_cstr((SzString *)sz_adt_payload(m));
    if (script_eq_mod(s, "shift"))
      mods |= SZ_KEY_SHIFT;
    else if (script_eq_mod(s, "ctrl"))
      mods |= SZ_KEY_CTRL;
    else if (script_eq_mod(s, "cmd"))
      mods |= SZ_KEY_CMD;
    else if (script_eq_mod(s, "alt"))
      mods |= SZ_KEY_ALT;
  }
  sz_release(xs);
  return mods;
}

static void play_script_event_json(SzUiSession *session, SzAdt *ev) {
  const char *op;
  if (!ev || !sz_json_is_obj(ev))
    sz_panic("Ui.run: inject event must be an object");
  op = sz_jev_str(ev, "op");
  if (!op[0])
    sz_panic("Ui.run: inject event needs op");
  if (strcmp(op, "tap") == 0)
    script_tap(session, (int)sz_jev_int(ev, "i", 0));
  else if (strcmp(op, "xy") == 0) {
    if (!sz_jev_has(ev, "x") || !sz_jev_has(ev, "y"))
      sz_panic("Ui.run: inject xy needs x and y");
    script_xy(session, (float)sz_jev_num(ev, "x", 0.0),
              (float)sz_jev_num(ev, "y", 0.0));
  } else if (strcmp(op, "text") == 0) {
    SzInputEvent e;
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    memset(&e, 0, sizeof e);
    e.kind = SZ_INPUT_TEXT;
    e.text = sz_jev_str(ev, "value");
    if (script_focus_field(session, idx)) {
      if (!sz_ui_inject_sync(session, &e))
        fprintf(stderr, "scuzz: script text skipped (no text field)\n");
    }
  } else if (strcmp(op, "type") == 0) {
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    script_type(session, idx, sz_jev_str(ev, "value"));
  } else if (strcmp(op, "key") == 0)
    script_key(session, sz_jev_str(ev, "key"), sz_jev_str(ev, "text"),
               jev_mods(ev), sz_jev_bool(ev, "repeat"));
  else if (strcmp(op, "compose") == 0)
    script_compose(session, sz_jev_str(ev, "value"));
  else if (strcmp(op, "commit") == 0)
    script_compose(session, "");
  else if (strcmp(op, "caret") == 0) {
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    int off = (int)sz_jev_int(ev, "offset", 0);
    if (!sz_ui_session_set_caret(session, idx, off)) {
      if (idx < 0)
        fprintf(stderr, "scuzz: script caret skipped (no text field)\n");
      else
        fprintf(stderr, "scuzz: script caret %d skipped\n", idx);
    }
  } else if (strcmp(op, "select") == 0) {
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    int a = (int)sz_jev_int(ev, "start", 0);
    int b = (int)sz_jev_int(ev, "end", 0);
    if (!sz_ui_session_set_sel(session, idx, a, b)) {
      if (idx < 0)
        fprintf(stderr, "scuzz: script select skipped (no text field)\n");
      else
        fprintf(stderr, "scuzz: script select %d skipped\n", idx);
    }
  } else if (strcmp(op, "copy") == 0) {
    if (!sz_ui_session_copy(session))
      fprintf(stderr, "scuzz: script copy skipped (no text field)\n");
  } else if (strcmp(op, "cut") == 0) {
    if (!sz_ui_session_cut(session))
      fprintf(stderr, "scuzz: script cut skipped (no text field)\n");
  } else if (strcmp(op, "paste") == 0) {
    const char *payload = sz_jev_has(ev, "value") ? sz_jev_str(ev, "value") : NULL;
    if (!sz_ui_session_paste(session, payload))
      fprintf(stderr, "scuzz: script paste skipped (no text field)\n");
  } else if (strcmp(op, "drag") == 0) {
    if (!sz_jev_has(ev, "x1") || !sz_jev_has(ev, "y1") || !sz_jev_has(ev, "x2") ||
        !sz_jev_has(ev, "y2"))
      sz_panic("Ui.run: inject drag needs x1 y1 x2 y2");
    script_drag(session, (float)sz_jev_num(ev, "x1", 0.0),
                (float)sz_jev_num(ev, "y1", 0.0), (float)sz_jev_num(ev, "x2", 0.0),
                (float)sz_jev_num(ev, "y2", 0.0));
  } else if (strcmp(op, "hover") == 0) {
    if (!sz_jev_has(ev, "x") || !sz_jev_has(ev, "y"))
      sz_panic("Ui.run: inject hover needs x and y");
    script_hover(session, (float)sz_jev_num(ev, "x", 0.0),
                 (float)sz_jev_num(ev, "y", 0.0));
  } else if (strcmp(op, "secondary") == 0) {
    if (sz_jev_has(ev, "x") && sz_jev_has(ev, "y"))
      script_secondary_xy(session, (float)sz_jev_num(ev, "x", 0.0),
                          (float)sz_jev_num(ev, "y", 0.0));
    else
      script_secondary_n(session, (int)sz_jev_int(ev, "i", 0));
  } else if (strcmp(op, "pump") == 0) {
    int k = (int)sz_jev_int(ev, "k", 1);
    while (k-- > 1) {
      if (!sz_ui_pump_sync(session))
        sz_panic("Ui.run: script pump failed");
    }
  } else if (strcmp(op, "scroll") == 0) {
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    script_scroll(session, idx, (float)sz_jev_num(ev, "dy", 40.0));
  } else if (strcmp(op, "backspace") == 0) {
    int idx = sz_jev_has(ev, "i") ? (int)sz_jev_int(ev, "i", 0) : -1;
    script_backspace(session, idx, (int)sz_jev_int(ev, "count", 1));
  } else if (strcmp(op, "dump") == 0)
    sz_ui_session_dump_now(session);
  else if (strcmp(op, "reload") == 0) {
    if (!sz_ui_session_reload(session))
      fprintf(stderr, "scuzz: script reload skipped (no factory)\n");
  } else if (strcmp(op, "quit") == 0) {
    sz_ui_session_request_stop(session);
    return;
  } else if (strcmp(op, "resetpeak") == 0) {
    sz_alloc_reset_stats();
    sz_alloc_mark();
  } else if (strcmp(op, "drive") == 0)
    sz_script_drive_json(ev);
  else
    sz_panic("Ui.run: unknown inject op");
  script_after_event(session);
}

void sz_ui_script_play_json(SzUiSession *session, const char *text) {
  SzString *doc;
  SzAdt *parsed, *json, *events;
  SzList *xs;
  if (!session || !text)
    return;
  doc = sz_string_from_cstr(text);
  parsed = sz_json_parse(doc);
  sz_release(doc);
  if (!parsed || sz_adt_tag(parsed) != 1)
    sz_panic("Ui.run: inject JSON parse failed");
  json = (SzAdt *)sz_adt_payload(parsed);
  if (!sz_json_is_obj(json) || sz_jev_int(json, "v", 0) != 1 ||
      strcmp(sz_jev_str(json, "kind"), "inject") != 0 ||
      !sz_jev_has(json, "events")) {
    sz_release(parsed);
    sz_panic("Ui.run: inject JSON wants {\"v\":1,\"kind\":\"inject\",\"events\":[...]}");
  }
  events = sz_jev_key(json, "events");
  if (!sz_json_is_arr(events)) {
    sz_release(parsed);
    sz_panic("Ui.run: inject events must be an array");
  }
  xs = sz_json_arr(events);
  {
    SzList *p;
    for (p = xs; p && !sz_list_is_empty(p); p = sz_list_tail(p)) {
      if (!sz_ui_session_alive(session))
        break;
      play_script_event_json(session, (SzAdt *)sz_list_head(p));
    }
  }
  sz_release(xs);
  sz_release(parsed);
}

void sz_ui_script_run_file(SzUiSession *session, const char *path) {
  FILE *f;
  char *line = NULL;
  size_t cap = 0;
  size_t len = 0;
  int c;
  if (!script_path_is_json(path))
    sz_panic("Ui.run: SCUZZ_UI_SCRIPT path must end in .json");
  f = fopen(path, "r");
  if (!f)
    sz_panic("Ui.run: SCUZZ_UI_SCRIPT open failed");
  for (;;) {
    char t[2];
    c = fgetc(f);
    if (c == EOF)
      break;
    t[0] = (char)c;
    t[1] = '\0';
    sz_dump_append(&line, &len, &cap, t);
  }
  fclose(f);
  if (line) {
    sz_ui_script_play_json(session, line);
    sz_free(line);
  }
}

