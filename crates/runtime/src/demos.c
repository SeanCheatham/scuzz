#include "scalui_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From ui.c */
void su_ui_resolve_headless_size(int *width, int *height, double *scale);
void su_ui_demo_finish(SuUiSession *session);

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

static void fill_cfg(SuUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  const char *rt = getenv("SCALUI_UI_RUNTIME");
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = SU_UI_RUNTIME_HEADLESS;
  if (rt && (strcmp(rt, "window") == 0 || strcmp(rt, "Window") == 0))
    cfg->kind = SU_UI_RUNTIME_WINDOW;
  cfg->width = width;
  cfg->height = height;
  cfg->title = "ScalUI";
  su_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
}

static void maybe_scripted_tap(SuUiSession *session) {
  SuInputEvent tap;
  if (!getenv("SCALUI_UI_TAP"))
    return;
  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = (float)su_ui_session_width(session) / 2.f;
  tap.y = (float)su_ui_session_height(session) / 2.f;
  /* Prefer primary control region if known via env coords. */
  {
    const char *sx = getenv("SCALUI_UI_TAP_X");
    const char *sy = getenv("SCALUI_UI_TAP_Y");
    if (sx)
      tap.x = (float)atof(sx);
    if (sy)
      tap.y = (float)atof(sy);
  }
  if (!su_ui_inject_sync(session, &tap) || !su_ui_pump_sync(session))
    su_panic("headless tap/pump failed");
}

/* --- hello label --------------------------------------------------------- */

typedef struct {
  char *text;
  int width;
  int height;
} HeadlessDemoEnv;

static void *thunk_headless_demo(void *env) {
  HeadlessDemoEnv *e = (HeadlessDemoEnv *)env;
  SuUiConfig cfg;
  SuView *view;
  SuUiSession *session;

  fill_cfg(&cfg, e->width, e->height);
  view = su_view_label(e->text, 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  if (!session)
    su_panic("headless mount failed");
  su_ui_session_take_root(session);

  if (!su_ui_pump_sync(session))
    su_panic("headless pump failed");
  maybe_scripted_tap(session);
  su_ui_demo_finish(session);
  su_ui_unmount(session);
  su_free(e->text);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_headless_label(const char *text, int width, int height) {
  HeadlessDemoEnv *e = (HeadlessDemoEnv *)su_alloc(sizeof(HeadlessDemoEnv));
  e->text = su_strdup(text ? text : "ScalUI");
  e->width = width;
  e->height = height;
  return su_io_delay(thunk_headless_demo, e);
}

/* --- Counter ------------------------------------------------------------- */

typedef struct {
  SuSignalInt *count;
} CounterTapEnv;

static void counter_on_tap(SuView *self, void *env) {
  CounterTapEnv *e = (CounterTapEnv *)env;
  (void)self;
  su_signal_int_set(e->count, su_signal_int_get(e->count) + 1);
}

typedef struct {
  SuUiSession *session;
  SuSignalInt *count;
  int64_t value;
} BridgeHopEnv;

static void *thunk_bridge_hop(void *env) {
  BridgeHopEnv *e = (BridgeHopEnv *)env;
  /* Completed IO posts a signal write; pump applies it on the UI thread. */
  su_ui_bridge_post_int(e->session, e->count, e->value);
  return NULL;
}

typedef struct {
  int width;
  int height;
} SizeEnv;

static void *thunk_counter(void *env) {
  SizeEnv *e = (SizeEnv *)env;
  SuUiConfig cfg;
  SuSignalInt *count;
  CounterTapEnv *tap_env;
  SuView *root, *row, *icon, *img;
  SuUiSession *session;
  BridgeHopEnv *hop;
  SuIoResult hop_result;

  fill_cfg(&cfg, e->width, e->height);
  count = su_signal_int(0);
  tap_env = (CounterTapEnv *)su_alloc(sizeof(CounterTapEnv));
  tap_env->count = count;

  root = su_view_column();
  su_view_add_child(root, su_view_text("Counter"));
  su_view_add_child(root, su_view_text_signal_int(count, "count = "));
  row = su_view_row();
  su_view_add_child(row, su_view_button("+1", counter_on_tap, tap_env));
  icon = su_view_icon('+', 0xFF142850u);
  su_view_add_child(row, icon);
  img = su_view_image(24, 24, 0xFF3D7EA6u, "");
  su_view_add_child(row, img);
  su_view_add_child(root, row);

  session = su_ui_mount(&cfg, root);
  if (!session)
    su_panic("counter mount failed");
  su_ui_session_take_root(session);

  if (!su_ui_pump_sync(session))
    su_panic("counter pump failed");

  /* Prove IO → UI bridge: a completed IO schedules count = 10, then pump. */
  if (getenv("SCALUI_UI_BRIDGE")) {
    hop = (BridgeHopEnv *)su_alloc(sizeof(BridgeHopEnv));
    hop->session = session;
    hop->count = count;
    hop->value = 10;
    hop_result = su_io_unsafe_run(su_io_delay(thunk_bridge_hop, hop));
    if (!hop_result.ok)
      su_panic("bridge hop IO failed");
    if (!su_ui_pump_sync(session))
      su_panic("bridge pump failed");
  }

  /* Scripted tap: locate +1 button via hit-test scan. */
  if (getenv("SCALUI_UI_TAP")) {
    SuInputEvent tap;
    SuView *r = su_ui_session_root(session);
    SuView *hit_btn = NULL;
    float tx = 40.f, ty = 60.f;
    int yi, xi;
    for (yi = 0; yi < cfg.height && !hit_btn; yi += 4) {
      for (xi = 0; xi < cfg.width; xi += 4) {
        SuView *h = su_view_hit_test(r, (float)xi, (float)yi);
        if (h && su_view_kind(h) == SU_VIEW_BUTTON) {
          SuRect fr = su_view_frame(h);
          hit_btn = h;
          tx = fr.x + fr.w * 0.5f;
          ty = fr.y + fr.h * 0.5f;
          break;
        }
      }
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
      su_panic("counter tap/pump failed");
  }

  su_ui_demo_finish(session);
  su_ui_unmount(session);
  su_signal_int_free(count);
  su_free(tap_env);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_counter(int width, int height) {
  SizeEnv *e = (SizeEnv *)su_alloc(sizeof(SizeEnv));
  e->width = width;
  e->height = height;
  return su_io_delay(thunk_counter, e);
}

/* --- Todo (IO Resource load/save) ---------------------------------------- */

#define TODO_MAX_ITEMS 32

typedef struct {
  char *items[TODO_MAX_ITEMS];
  int count;
  SuSignalStr *draft;
  SuView *list;
  char *path;
} TodoState;

static void todo_rebuild_list(TodoState *st) {
  /* Replace list children by freeing old list node contents via rebuild:
   * Phase 2 keeps it simple — clear by recreating labels as add-only during
   * the session; initial load populates before mount. */
  int i;
  for (i = 0; i < st->count; i++) {
    char line[128];
    snprintf(line, sizeof line, "- %s", st->items[i] ? st->items[i] : "");
    su_view_add_child(st->list, su_view_text(line));
  }
}

typedef struct {
  TodoState *st;
  int writing; /* 0=load (r), 1=save (w) */
} TodoFileEnv;

static void *todo_acquire(void *env) {
  TodoFileEnv *fe = (TodoFileEnv *)env;
  FILE *f = fopen(fe->st->path, fe->writing ? "w" : "r");
  if (!f && !fe->writing) {
    /* Missing file is an empty list. */
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
  TodoState *st = fe->st;
  FILE *f = (FILE *)acquired;
  char line[256];
  st->count = 0;
  while (st->count < TODO_MAX_ITEMS && fgets(line, sizeof line, f)) {
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
  TodoState *st = fe->st;
  FILE *f = (FILE *)acquired;
  int i;
  for (i = 0; i < st->count; i++) {
    if (st->items[i])
      fprintf(f, "%s\n", st->items[i]);
  }
  fflush(f);
  return su_io_pure(NULL);
}

static void todo_on_add(SuView *self, void *env) {
  TodoState *st = (TodoState *)env;
  const char *draft;
  char line[128];
  (void)self;
  draft = su_signal_str_get(st->draft);
  if (!draft || !draft[0] || st->count >= TODO_MAX_ITEMS)
    return;
  st->items[st->count++] = su_strdup(draft);
  snprintf(line, sizeof line, "- %s", draft);
  su_view_add_child(st->list, su_view_text(line));
  su_signal_str_set(st->draft, "");
}

static void todo_on_save(SuView *self, void *env) {
  TodoState *st = (TodoState *)env;
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

static void todo_free_state(TodoState *st) {
  int i;
  if (!st)
    return;
  for (i = 0; i < st->count; i++)
    su_free(st->items[i]);
  su_signal_str_free(st->draft);
  su_free(st->path);
  su_free(st);
}

static void *thunk_todo(void *env) {
  SizeEnv *e = (SizeEnv *)env;
  SuUiConfig cfg;
  TodoState *st;
  SuResource *res;
  SuIoResult r;
  SuView *root, *row, *scroll;
  SuUiSession *session;
  const char *path_env;

  fill_cfg(&cfg, e->width, e->height);
  /* Default headless size for todo is larger when unset. */
  if (!getenv("SCALUI_UI_WIDTH") && e->width <= 0)
    cfg.width = 240;
  if (!getenv("SCALUI_UI_HEIGHT") && e->height <= 0)
    cfg.height = 160;

  st = (TodoState *)su_alloc_zero(sizeof(TodoState));
  path_env = getenv("SCALUI_TODO_PATH");
  st->path = su_strdup(path_env && path_env[0] ? path_env : "/tmp/scalui_todo.txt");
  st->draft = su_signal_str("");
  st->list = su_view_list();

  {
    TodoFileEnv fe;
    fe.st = st;
    fe.writing = 0;
    res = su_resource_make(todo_acquire, todo_release, &fe);
    r = su_io_unsafe_run(su_resource_use(res, todo_use_load, &fe));
    su_resource_free(res);
  }
  if (!r.ok)
    su_panic("todo load failed");
  todo_rebuild_list(st);

  root = su_view_column();
  su_view_add_child(root, su_view_text("Todo"));
  row = su_view_row();
  su_view_add_child(row, su_view_text_field(st->draft, "item"));
  su_view_add_child(row, su_view_button("Add", todo_on_add, st));
  su_view_add_child(root, row);
  /* Scroll hosts the list so long todos remain addressable. */
  scroll = su_view_scroll(st->list);
  su_view_add_child(root, scroll);
  su_view_add_child(root, su_view_button("Save", todo_on_save, st));

  session = su_ui_mount(&cfg, root);
  if (!session)
    su_panic("todo mount failed");
  su_ui_session_take_root(session);

  if (!su_ui_pump_sync(session))
    su_panic("todo pump failed");

  /* Scripted: seed draft + tap Add (hit-test finds the button). */
  if (getenv("SCALUI_UI_TAP")) {
    SuInputEvent ev;
    const char *seed = getenv("SCALUI_TODO_SEED");
    SuView *root_v = su_ui_session_root(session);
    SuView *add_btn = NULL;
    float tx = 0.f, ty = 0.f;
    int yi, xi;
    memset(&ev, 0, sizeof(ev));
    ev.kind = SU_INPUT_TEXT;
    ev.text = seed && seed[0] ? seed : "milk";
    if (!su_ui_inject_sync(session, &ev))
      su_panic("todo text inject failed");
    /* Scan for the Add button after layout from the last pump. */
    for (yi = 0; yi < cfg.height && !add_btn; yi += 4) {
      for (xi = 0; xi < cfg.width; xi += 4) {
        SuView *h = su_view_hit_test(root_v, (float)xi, (float)yi);
        if (h && su_view_kind(h) == SU_VIEW_BUTTON) {
          SuRect fr = su_view_frame(h);
          /* Prefer the upper Add button over Save. */
          if (!add_btn || fr.y < su_view_frame(add_btn).y) {
            add_btn = h;
            tx = fr.x + fr.w * 0.5f;
            ty = fr.y + fr.h * 0.5f;
          }
        }
      }
    }
    if (!add_btn)
      su_panic("todo: Add button not found");
    memset(&ev, 0, sizeof(ev));
    ev.kind = SU_INPUT_TAP;
    ev.x = tx;
    ev.y = ty;
    {
      const char *sx = getenv("SCALUI_UI_TAP_X");
      const char *sy = getenv("SCALUI_UI_TAP_Y");
      if (sx)
        ev.x = (float)atof(sx);
      if (sy)
        ev.y = (float)atof(sy);
    }
    if (!su_ui_inject_sync(session, &ev) || !su_ui_pump_sync(session))
      su_panic("todo add tap failed");
  }

  if (getenv("SCALUI_TODO_SAVE"))
    todo_on_save(NULL, st);

  su_ui_demo_finish(session);
  su_ui_unmount(session);
  todo_free_state(st);
  su_free(e);
  return NULL;
}

SuIo *su_ui_run_todo(int width, int height) {
  SizeEnv *e = (SizeEnv *)su_alloc(sizeof(SizeEnv));
  e->width = width;
  e->height = height;
  return su_io_delay(thunk_todo, e);
}
