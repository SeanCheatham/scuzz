#define _POSIX_C_SOURCE 200809L

#include "scalui_ui.h"
#include "scalui_embedder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* fill_cfg / demo_finish live in demos.c / ui.c */
void su_ui_resolve_headless_size(int *width, int *height, double *scale);
void su_ui_demo_finish(SuUiSession *session);

static void fill_cfg(SuUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  const char *rt = getenv("SCALUI_UI_RUNTIME");
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = SU_UI_RUNTIME_HEADLESS;
  if (rt && (strcmp(rt, "window") == 0 || strcmp(rt, "Window") == 0))
    cfg->kind = SU_UI_RUNTIME_WINDOW;
  else if (rt && (strcmp(rt, "mobile") == 0 || strcmp(rt, "Mobile") == 0))
    cfg->kind = SU_UI_RUNTIME_MOBILE;
  cfg->width = width;
  cfg->height = height;
  cfg->title = "ScalUI";
  su_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
}

static char *su_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)su_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

/* --- Signal -------------------------------------------------------------- */

SuSignalInt *su_lang_signal_int(int64_t initial) { return su_signal_int(initial); }

int64_t su_lang_signal_get(SuSignalInt *s) { return su_signal_int_get(s); }

void *su_lang_signal_set(SuSignalInt *s, int64_t v) {
  su_signal_int_set(s, v);
  return NULL;
}

SuSignalStr *su_lang_signal_str(SuString *initial) {
  return su_signal_str(initial ? su_string_cstr(initial) : "");
}

void *su_lang_signal_str_set(SuSignalStr *s, SuString *v) {
  su_signal_str_set(s, v ? su_string_cstr(v) : "");
  return NULL;
}

/* --- View builders ------------------------------------------------------- */

SuView *su_lang_view_text(SuString *text) {
  return su_view_text(text ? su_string_cstr(text) : "");
}

SuView *su_lang_view_text_signal(SuSignalInt *sig, SuString *prefix) {
  return su_view_text_signal_int(sig, prefix ? su_string_cstr(prefix) : "");
}

/* First-class tap closure: `tap`/`env` are a compiled `_ => ...` lambda's
 * function pointer + captured-locals env (ADR-driven ScalUI closures). */
SuView *su_lang_view_button(SuString *label, SuViewTapFn tap, void *env) {
  return su_view_button(label ? su_string_cstr(label) : "", tap, env);
}

SuView *su_lang_view_column(void) { return su_view_column(); }
SuView *su_lang_view_row(void) { return su_view_row(); }
SuView *su_lang_view_list(void) { return su_view_list(); }

SuView *su_lang_view_scroll(SuView *child) { return su_view_scroll(child); }

SuView *su_lang_view_text_field(SuSignalStr *text, SuString *placeholder) {
  return su_view_text_field(text, placeholder ? su_string_cstr(placeholder) : "");
}

SuView *su_lang_view_icon(int64_t glyph, int64_t argb) {
  return su_view_icon((char)glyph, (uint32_t)argb);
}

SuView *su_lang_view_image(int64_t w, int64_t h, int64_t argb, SuString *caption) {
  return su_view_image((int)w, (int)h, (uint32_t)argb,
                       caption ? su_string_cstr(caption) : "");
}

void *su_lang_view_add_child(SuView *parent, SuView *child) {
  su_view_add_child(parent, child);
  return NULL;
}

SuView *su_lang_view_show_when(SuSignalInt *sig, int64_t value, SuView *child) {
  return su_view_show_when(sig, value, child);
}

/* --- Todo controller ----------------------------------------------------- */

struct SuTodo {
  char *items[32];
  int count;
  SuSignalStr *draft;
  SuView *list;
  char *path;
};

SuTodo *su_lang_todo_create(void) {
  SuTodo *st = (SuTodo *)su_alloc_zero(sizeof(SuTodo));
  const char *path_env = getenv("SCALUI_TODO_PATH");
  st->path = su_strdup(path_env && path_env[0] ? path_env : "/tmp/scalui_todo.txt");
  st->draft = su_signal_str("");
  st->list = su_view_list();
  return st;
}

SuSignalStr *su_lang_todo_draft(SuTodo *todo) { return todo ? todo->draft : NULL; }

SuView *su_lang_todo_list_view(SuTodo *todo) { return todo ? todo->list : NULL; }

typedef struct {
  SuTodo *st;
  int writing;
} TodoFileEnv;

static void *todo_acquire(void *env) {
  TodoFileEnv *fe = (TodoFileEnv *)env;
  FILE *f = fopen(fe->st->path, fe->writing ? "w" : "r");
  if (!f && !fe->writing) {
    f = fopen(fe->st->path, "w+");
    if (f)
      rewind(f);
  }
  if (!f)
    su_panic("todo: open failed");
  return f;
}

static void todo_release(void *acquired, void *env) {
  (void)env;
  if (acquired)
    fclose((FILE *)acquired);
}

static SuIo *todo_use_load(void *acquired, void *env) {
  TodoFileEnv *fe = (TodoFileEnv *)env;
  SuTodo *st = fe->st;
  FILE *f = (FILE *)acquired;
  char line[256];
  st->count = 0;
  while (st->count < 32 && fgets(line, sizeof line, f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
      line[--n] = '\0';
    if (n == 0)
      continue;
    st->items[st->count++] = su_strdup(line);
  }
  return su_io_pure(NULL);
}

static SuIo *todo_use_save(void *acquired, void *env) {
  TodoFileEnv *fe = (TodoFileEnv *)env;
  SuTodo *st = fe->st;
  FILE *f = (FILE *)acquired;
  int i;
  for (i = 0; i < st->count; i++) {
    if (st->items[i])
      fprintf(f, "%s\n", st->items[i]);
  }
  fflush(f);
  return su_io_pure(NULL);
}

static void todo_rebuild_list(SuTodo *st) {
  int i;
  for (i = 0; i < st->count; i++) {
    char line[128];
    snprintf(line, sizeof line, "- %s", st->items[i] ? st->items[i] : "");
    su_view_add_child(st->list, su_view_text(line));
  }
}

static void *thunk_todo_load(void *env) {
  SuTodo *st = (SuTodo *)env;
  TodoFileEnv fe;
  SuResource *res;
  SuIoResult r;
  fe.st = st;
  fe.writing = 0;
  res = su_resource_make(todo_acquire, todo_release, &fe);
  r = su_io_unsafe_run(su_resource_use(res, todo_use_load, &fe));
  su_resource_free(res);
  if (!r.ok)
    su_panic("todo load failed");
  todo_rebuild_list(st);
  return NULL;
}

SuIo *su_lang_todo_load(SuTodo *todo) { return su_io_delay(thunk_todo_load, todo); }

static void todo_on_add(SuView *self, void *env) {
  SuTodo *st = (SuTodo *)env;
  const char *draft;
  char line[128];
  (void)self;
  draft = su_signal_str_get(st->draft);
  if (!draft || !draft[0] || st->count >= 32)
    return;
  st->items[st->count++] = su_strdup(draft);
  snprintf(line, sizeof line, "- %s", draft);
  su_view_add_child(st->list, su_view_text(line));
  su_signal_str_set(st->draft, "");
}

static void todo_on_save(SuView *self, void *env) {
  SuTodo *st = (SuTodo *)env;
  TodoFileEnv fe;
  SuResource *res;
  SuIoResult r;
  (void)self;
  fe.st = st;
  fe.writing = 1;
  res = su_resource_make(todo_acquire, todo_release, &fe);
  r = su_io_unsafe_run(su_resource_use(res, todo_use_save, &fe));
  su_resource_free(res);
  if (!r.ok)
    su_panic("todo save failed");
}

SuView *su_lang_view_button_todo_add(SuString *label, SuTodo *todo) {
  return su_view_button(label ? su_string_cstr(label) : "Add", todo_on_add, todo);
}

SuView *su_lang_view_button_todo_save(SuString *label, SuTodo *todo) {
  return su_view_button(label ? su_string_cstr(label) : "Save", todo_on_save, todo);
}

static void todo_free(SuTodo *st) {
  int i;
  if (!st)
    return;
  for (i = 0; i < st->count; i++)
    su_free(st->items[i]);
  su_signal_str_free(st->draft);
  su_free(st->path);
  su_free(st);
}

/* --- Ui.run / Ui.runWithTodo --------------------------------------------- */

typedef struct {
  SuView *root;
  SuTodo *todo; /* optional; enables todo tap scripting */
} RunViewEnv;

static void scripted_button_tap(SuUiSession *session, int prefer_upper) {
  SuInputEvent tap;
  SuView *r = su_ui_session_root(session);
  SuView *hit_btn = NULL;
  SuView *buttons[64];
  int n_buttons = 0;
  float tx = 40.f, ty = 60.f;
  int yi, xi;
  int w = su_ui_session_width(session);
  int h = su_ui_session_height(session);
  const char *tap_n_env = getenv("SCALUI_UI_TAP_N");
  int tap_n = (tap_n_env && tap_n_env[0]) ? atoi(tap_n_env) : -1;

  /* Collect unique buttons in top-to-bottom, left-to-right scan order. */
  for (yi = 0; yi < h; yi += 4) {
    for (xi = 0; xi < w; xi += 4) {
      SuView *hit = su_view_hit_test(r, (float)xi, (float)yi);
      if (hit && su_view_kind(hit) == SU_VIEW_BUTTON) {
        int seen = 0;
        int bi;
        for (bi = 0; bi < n_buttons; bi++) {
          if (buttons[bi] == hit) {
            seen = 1;
            break;
          }
        }
        if (!seen && n_buttons < 64)
          buttons[n_buttons++] = hit;
      }
    }
  }

  if (tap_n >= 0) {
    if (tap_n >= n_buttons)
      su_panic("Ui.run: SCALUI_UI_TAP_N out of range");
    hit_btn = buttons[tap_n];
  } else if (prefer_upper && n_buttons > 0) {
    int bi;
    hit_btn = buttons[0];
    for (bi = 1; bi < n_buttons; bi++) {
      if (su_view_frame(buttons[bi]).y < su_view_frame(hit_btn).y)
        hit_btn = buttons[bi];
    }
  } else if (n_buttons > 0) {
    hit_btn = buttons[0];
  }

  if (!hit_btn)
    su_panic("Ui.run: button not found for SCALUI_UI_TAP");
  {
    SuRect fr = su_view_frame(hit_btn);
    tx = fr.x + fr.w * 0.5f;
    ty = fr.y + fr.h * 0.5f;
  }
  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = tx;
  tap.y = ty;
  {
    const char *sx = getenv("SCALUI_UI_TAP_X");
    const char *sy = getenv("SCALUI_UI_TAP_Y");
    if (sx)
      tap.x = (float)atof(sx);
    if (sy)
      tap.y = (float)atof(sy);
  }
  if (!su_ui_inject_sync(session, &tap) || !su_ui_pump_sync(session))
    su_panic("Ui.run tap/pump failed");
}

static void *thunk_run_view(void *env) {
  RunViewEnv *e = (RunViewEnv *)env;
  SuUiConfig cfg;
  SuUiSession *session;
  int interactive;

  fill_cfg(&cfg, 0, 0);
  if (e->todo) {
    if (!getenv("SCALUI_UI_WIDTH"))
      cfg.width = 240;
    if (!getenv("SCALUI_UI_HEIGHT"))
      cfg.height = 160;
  }

  session = su_ui_mount(&cfg, e->root);
  if (!session)
    su_panic("Ui.run mount failed");
  su_ui_session_take_root(session);

  if (!su_ui_pump_sync(session))
    su_panic("Ui.run pump failed");

  if (getenv("SCALUI_UI_TAP")) {
    if (e->todo) {
      SuInputEvent ev;
      const char *seed = getenv("SCALUI_TODO_SEED");
      memset(&ev, 0, sizeof(ev));
      ev.kind = SU_INPUT_TEXT;
      ev.text = seed && seed[0] ? seed : "milk";
      if (!su_ui_inject_sync(session, &ev))
        su_panic("todo text inject failed");
      scripted_button_tap(session, 1);
    } else {
      scripted_button_tap(session, 0);
    }
  }

  if (e->todo && getenv("SCALUI_TODO_SAVE"))
    todo_on_save(NULL, e->todo);

  interactive = cfg.kind == SU_UI_RUNTIME_WINDOW && su_embedder_available();
  if (interactive) {
    const char *max_frames_env = getenv("SCALUI_LIVE_FRAMES");
    int64_t max_frames =
        (max_frames_env && atoi(max_frames_env) > 0) ? atoi(max_frames_env) : 0;
    int64_t frame = 0;
    do {
      if (!su_ui_pump_sync(session))
        su_panic("Ui.run live pump failed");
      frame++;
      if (max_frames > 0 && frame >= max_frames)
        break;
      {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 16000000L; /* ~60fps cap */
        nanosleep(&ts, NULL);
      }
    } while (su_embedder_alive());
  } else {
    su_ui_demo_finish(session);
  }

  su_ui_unmount(session);
  if (e->todo)
    todo_free(e->todo);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_view(SuView *root) {
  RunViewEnv *e = (RunViewEnv *)su_alloc(sizeof(RunViewEnv));
  e->root = root;
  e->todo = NULL;
  return su_io_delay(thunk_run_view, e);
}

SuIo *su_ui_run_view_todo(SuView *root, SuTodo *todo) {
  RunViewEnv *e = (RunViewEnv *)su_alloc(sizeof(RunViewEnv));
  e->root = root;
  e->todo = todo;
  return su_io_delay(thunk_run_view, e);
}
