#include "ui_script.h"

#include "scuzz_rt.h"

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

static void script_parse_scroll(const char *rest, int *index, float *dy) {
  const char *p = rest ? rest : "";
  int a = 0;
  *index = -1;
  *dy = 40.f;
  while (*p == ' ')
    p++;
  if (!*p)
    return;
  if (*p == '-') {
    *dy = (float)atoi(p);
    return;
  }
  if (*p < '0' || *p > '9')
    return;
  while (*p >= '0' && *p <= '9') {
    a = a * 10 + (*p - '0');
    p++;
  }
  while (*p == ' ')
    p++;
  if (*p) {
    *index = a;
    *dy = (float)atoi(p);
    return;
  }
  *dy = (float)a;
}

static void script_scroll(SzUiSession *session, int index, float dy) {
  SzView *scrolls[64];
  int count = sz_ui_collect_scrolls(session, scrolls, 64);
  SzInputEvent ev;
  SzRect fr;
  int n = index < 0 ? 0 : index;
  if (count <= 0 || n >= count) {
    if (index < 0)
      fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
    else
      fprintf(stderr, "scuzz: script scroll %d skipped (%d scrolls)\n", n, count);
    return;
  }
  fr = sz_view_frame(scrolls[n]);
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = fr.x + fr.w * 0.5f;
  ev.y = fr.y + fr.h * 0.5f;
  ev.dy = dy;
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script scroll skipped (no scroll)\n");
}

/* `N payload` → index N and payload; otherwise index -1 and rest unchanged
 * so `text 0` still means replace-with-"0" on the starred field. */
static const char *script_field_payload(const char *rest, int *index) {
  const char *p = rest ? rest : "";
  int n = 0;
  *index = -1;
  if (*p < '0' || *p > '9')
    return p;
  while (*p >= '0' && *p <= '9') {
    n = n * 10 + (*p - '0');
    p++;
  }
  if (*p == ' ') {
    *index = n;
    return p + 1;
  }
  return rest ? rest : "";
}

static void script_parse_backspace(const char *rest, int *index, int *count) {
  const char *p = rest ? rest : "";
  int a = 0, b = 0;
  *index = -1;
  *count = 1;
  if (*p < '0' || *p > '9')
    return;
  while (*p >= '0' && *p <= '9') {
    a = a * 10 + (*p - '0');
    p++;
  }
  if (*p == ' ') {
    p++;
    if (*p >= '0' && *p <= '9') {
      while (*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
      }
      *index = a;
      *count = b < 1 ? 1 : b;
      return;
    }
  }
  *count = a < 1 ? 1 : a;
}

static void script_parse_caret(const char *rest, int *index, int *offset) {
  const char *p = rest ? rest : "";
  int a = 0, b = 0;
  *index = -1;
  *offset = 0;
  if (*p < '0' || *p > '9')
    return;
  while (*p >= '0' && *p <= '9') {
    a = a * 10 + (*p - '0');
    p++;
  }
  if (*p == ' ') {
    p++;
    if (*p >= '0' && *p <= '9') {
      while (*p >= '0' && *p <= '9') {
        b = b * 10 + (*p - '0');
        p++;
      }
      *index = a;
      *offset = b;
      return;
    }
  }
  *offset = a;
}

static int script_focus_field(SzUiSession *session, int index) {
  if (session && sz_view_focus_text_field_at(sz_ui_session_root(session), index))
    return 1;
  if (index < 0)
    fprintf(stderr, "scuzz: script skipped (no text field)\n");
  else
    fprintf(stderr, "scuzz: script field %d skipped\n", index);
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

static void script_key(SzUiSession *session, const char *name, const char *text) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_KEY;
  ev.key = name ? name : "";
  ev.text = text ? text : "";
  if (!sz_ui_inject_sync(session, &ev))
    fprintf(stderr, "scuzz: script key skipped\n");
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
/* Line protocol, one event per line, delivered across pump boundaries:
     tap <n>    activate the nth tap target in a11y preorder ([taps] in the dump); missing target is a no-op
     xy <x> <y> inject TAP at logical point; miss does not panic
     text <s>   replace the [fields] starred TextField with <s>; no field is a no-op
     text <n> <s>  replace dump-index n (a11y order); `text 0` is still payload "0"
     type <s>   insert <s> at the caret on the [fields] starred TextField; empty is a no-op; no field is a no-op
     type <n> <s>  insert at the caret on dump-index n; `type 0` is still payload "0"
     key <name> [text]  named key on the starred TextField (`Enter`, `Backspace`, `ArrowLeft`, `a`);
                        optional UTF-8 insert text; arrows / Home / End / Delete use the caret;
                        live OS keys record this verb
     caret <n>  set the starred TextField caret to byte offset n
     caret <i> <n>  set dump-index i caret to byte offset n
     hover <x> <y>  pointer MOVE with no button (hover); shows View.tooltip; live OS hover records this verb
     secondary <n>  button-3 click on tap-dump index n; does not fire the primary tap
     secondary <x> <y>  button-3 click at a logical point; live OS right-click records this verb
     pump <k>   pump k extra frames
     scroll <dy> pan the first Scroll on its axis (positive = content up or left); no scroll is a no-op
     scroll <n> <dy>  pan dump-index n ([scrolls] scan order); `scroll 40` stays dy 40
     backspace <n> chop n UTF-8 code points before the caret on the [fields] starred TextField (default 1); no field is a no-op
     backspace <n> <k>  chop k UTF-8 code points before the caret on dump-index n
     dump       rewrite the live debug dump now (includes [heap] kinds, delta, and [live] rows); no dump path is a no-op
     reload     rebuild the View factory now; missing factory is a no-op
     quit       stop the live session; remaining script lines do not run
     resetpeak  set peak_bytes to live and mark delta; next dump reports growth from here
     drive <name> [args]  run a verify-graph driver (Int/String/Bool args)
   Blank lines and #-comments are skipped. Pump runs after every event except quit. */

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

static void play_script_line(SzUiSession *session, char *line) {
  size_t len = strlen(line);
  if (len == 0 || line[0] == '#')
    return;
  if (strncmp(line, "tap ", 4) == 0 || strcmp(line, "tap") == 0)
    script_tap(session, len > 3 ? atoi(line + 4) : 0);
  else if (strncmp(line, "xy ", 3) == 0) {
    float x = 0.f, y = 0.f;
    if (sscanf(line + 3, "%f %f", &x, &y) == 2)
      script_xy(session, x, y);
    else
      sz_panic("Ui.run: xy needs x y");
  } else if (strncmp(line, "text ", 5) == 0 || strcmp(line, "text") == 0) {
    SzInputEvent ev;
    int idx;
    const char *payload = script_field_payload(len > 4 ? line + 5 : "", &idx);
    memset(&ev, 0, sizeof(ev));
    ev.kind = SZ_INPUT_TEXT;
    ev.text = payload;
    if (script_focus_field(session, idx)) {
      if (!sz_ui_inject_sync(session, &ev))
        fprintf(stderr, "scuzz: script text skipped (no text field)\n");
    }
  } else if (strncmp(line, "pump ", 5) == 0 || strcmp(line, "pump") == 0) {
    int k = len > 5 ? atoi(line + 5) : 1;
    while (k-- > 1) {
      if (!sz_ui_pump_sync(session))
        sz_panic("Ui.run: script pump failed");
    }
  } else if (strncmp(line, "scroll ", 7) == 0 || strcmp(line, "scroll") == 0) {
    int idx;
    float dy;
    script_parse_scroll(len > 6 ? line + 7 : "", &idx, &dy);
    script_scroll(session, idx, dy);
  } else if (strncmp(line, "backspace ", 10) == 0 ||
             strcmp(line, "backspace") == 0) {
    int idx, n;
    script_parse_backspace(len > 9 ? line + 10 : "", &idx, &n);
    script_backspace(session, idx, n);
  } else if (strncmp(line, "type ", 5) == 0 || strcmp(line, "type") == 0) {
    int idx;
    const char *payload = script_field_payload(len > 4 ? line + 5 : "", &idx);
    script_type(session, idx, payload);
  } else if (strncmp(line, "key ", 4) == 0 || strcmp(line, "key") == 0) {
    const char *rest = len > 3 ? line + 4 : "";
    char name[64];
    const char *text = "";
    size_t ni = 0;
    while (*rest == ' ')
      rest++;
    while (rest[ni] && rest[ni] != ' ' && ni + 1 < sizeof name) {
      name[ni] = rest[ni];
      ni++;
    }
    name[ni] = '\0';
    if (rest[ni] == ' ')
      text = rest + ni + 1;
    script_key(session, name, text);
  } else if (strncmp(line, "caret ", 6) == 0 || strcmp(line, "caret") == 0) {
    int idx, off;
    script_parse_caret(len > 5 ? line + 6 : "", &idx, &off);
    if (!sz_ui_session_set_caret(session, idx, off)) {
      if (idx < 0)
        fprintf(stderr, "scuzz: script caret skipped (no text field)\n");
      else
        fprintf(stderr, "scuzz: script caret %d skipped\n", idx);
    }
  } else if (strncmp(line, "hover ", 6) == 0) {
    float x = 0.f, y = 0.f;
    if (sscanf(line + 6, "%f %f", &x, &y) == 2)
      script_hover(session, x, y);
    else
      sz_panic("Ui.run: hover needs x y");
  } else if (strncmp(line, "secondary ", 10) == 0 ||
             strcmp(line, "secondary") == 0) {
    float x = 0.f, y = 0.f;
    const char *rest = len > 9 ? line + 10 : "";
    if (sscanf(rest, "%f %f", &x, &y) == 2)
      script_secondary_xy(session, x, y);
    else
      script_secondary_n(session, rest[0] ? atoi(rest) : 0);
  } else if (strcmp(line, "dump") == 0) {
    sz_ui_session_dump_now(session);
  } else if (strcmp(line, "reload") == 0) {
    if (!sz_ui_session_reload(session))
      fprintf(stderr, "scuzz: script reload skipped (no factory)\n");
  } else if (strcmp(line, "quit") == 0) {
    sz_ui_session_request_stop(session);
    return;
  } else if (strcmp(line, "resetpeak") == 0) {
    sz_alloc_reset_stats();
    sz_alloc_mark();
  } else if (strncmp(line, "drive ", 6) == 0)
    sz_driver_run_line(line + 6);
  else
    sz_panic("Ui.run: unknown SCUZZ_UI_SCRIPT directive");
  if (!sz_ui_session_alive(session))
    return;
  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run: script pump failed");
}

void sz_ui_script_play_text(SzUiSession *session, char *text) {
  char *p = text;
  while (p && *p) {
    char *nl = strchr(p, '\n');
    char *line = p;
    size_t len;
    if (!sz_ui_session_alive(session))
      return;
    if (nl) {
      *nl = '\0';
      p = nl + 1;
    } else
      p += strlen(p);
    len = strlen(line);
    while (len > 0 && line[len - 1] == '\r')
      line[--len] = '\0';
    play_script_line(session, line);
  }
}

void sz_ui_script_run_file(SzUiSession *session, const char *path) {
  FILE *f = fopen(path, "r");
  char line[1024];
  if (!f)
    sz_panic("Ui.run: SCUZZ_UI_SCRIPT open failed");
  while (fgets(line, sizeof line, f)) {
    size_t len = strlen(line);
    if (!sz_ui_session_alive(session))
      break;
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    play_script_line(session, line);
  }
  fclose(f);
}
