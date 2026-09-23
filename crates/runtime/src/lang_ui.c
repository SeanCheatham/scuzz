#define _POSIX_C_SOURCE 200809L

#include "scuzz_ui.h"
#include "scuzz_embedder.h"
#include "scuzz_mobile.h"

#include "ui_script.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include "web.h"
#endif

/* Parse SCUZZ_UI_RUNTIME. Unset or empty means Headless. Reject any other
   unknown value: a typo must not silently run Headless. */
static SzUiRuntimeKind parse_runtime_kind(const char *rt) {
  if (!rt || !rt[0] || strcasecmp(rt, "headless") == 0)
    return SZ_UI_RUNTIME_HEADLESS;
  if (strcasecmp(rt, "desktop") == 0)
    return SZ_UI_RUNTIME_DESKTOP;
  if (strcasecmp(rt, "mobile") == 0)
    return SZ_UI_RUNTIME_MOBILE;
  sz_panic("SCUZZ_UI_RUNTIME must be headless, desktop, or mobile");
}

static void fill_cfg(SzUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = parse_runtime_kind(getenv("SCUZZ_UI_RUNTIME"));
#ifdef __EMSCRIPTEN__
  cfg->kind = SZ_UI_RUNTIME_WEB;
#endif
  cfg->width = width;
  cfg->height = height;
  cfg->title = sz_ui_default_title();
  sz_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
#ifdef __EMSCRIPTEN__
  cfg->scale = emscripten_get_device_pixel_ratio();
#endif
}

/* --- View builders ------------------------------------------------------- */

SzView *sz_lang_view_text(SzString *text) {
  return sz_view_text(text ? sz_string_cstr(text) : "");
}

SzView *sz_lang_view_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_icon_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_icon_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_fab(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_fab(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_outlined_button(SzString *label, SzViewTapFn tap,
                                    void *env) {
  return sz_view_outlined_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_text_button(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_text_button(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_vertical_divider(void) { return sz_view_vertical_divider(); }

SzView *sz_lang_view_checkbox(SzSignalInt *sig, SzString *label) {
  return sz_view_checkbox(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_radio(SzSignalInt *sig, int64_t value, SzString *label) {
  return sz_view_radio(sig, value, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_slider(SzSignalInt *sig) { return sz_view_slider(sig); }

SzView *sz_lang_view_progress(SzSignalInt *sig) { return sz_view_progress(sig); }

SzView *sz_lang_view_circular_progress(SzSignalInt *sig) {
  return sz_view_circular_progress(sig);
}

SzView *sz_lang_view_avatar(SzString *label) {
  return sz_view_avatar(label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_switch(SzSignalInt *sig, SzString *label) {
  return sz_view_switch(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_chip(SzSignalInt *sig, SzString *label) {
  return sz_view_chip(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_filter_chip(SzSignalInt *sig, SzString *label) {
  return sz_view_filter_chip(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_choice_chip(SzSignalInt *sig, int64_t value,
                                SzString *label) {
  return sz_view_choice_chip(sig, value, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_action_chip(SzString *label, SzViewTapFn tap, void *env) {
  return sz_view_action_chip(label ? sz_string_cstr(label) : "", tap, env);
}

SzView *sz_lang_view_input_chip(SzSignalInt *sig, SzString *label) {
  return sz_view_input_chip(sig, label ? sz_string_cstr(label) : "");
}

SzView *sz_lang_view_list_tile(SzString *title, SzView *trailing) {
  return sz_view_list_tile(title ? sz_string_cstr(title) : "", trailing);
}

SzView *sz_lang_view_checkbox_list_tile(SzSignalInt *sig, SzString *title) {
  return sz_view_checkbox_list_tile(sig, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_switch_list_tile(SzSignalInt *sig, SzString *title) {
  return sz_view_switch_list_tile(sig, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_radio_list_tile(SzSignalInt *sig, int64_t value,
                                    SzString *title) {
  return sz_view_radio_list_tile(sig, value, title ? sz_string_cstr(title) : "");
}

SzView *sz_lang_view_segmented(SzSignalInt *sig, SzString *left, SzString *right) {
  return sz_view_segmented(sig, left ? sz_string_cstr(left) : "",
                           right ? sz_string_cstr(right) : "");
}

SzView *sz_lang_view_badge(SzSignalInt *sig, SzView *child) {
  return sz_view_badge(sig, child);
}

SzView *sz_lang_view_section(SzString *id, SzString *title, SzView *child) {
  return sz_view_section(id ? sz_string_cstr(id) : "", title ? sz_string_cstr(title) : "", child);
}

SzView *sz_lang_view_index_book(SzSignalInt *selected, SzView *sections) {
  return sz_view_index_book(selected, sections);
}

SzView *sz_lang_view_app_shell(SzView *bar, SzView *body) {
  return sz_view_app_shell(bar, body);
}

SzView *sz_lang_view_app_bar(SzView *title, SzView *actions) {
  return sz_view_app_bar(title, actions);
}

SzView *sz_lang_view_tabs(SzSignalInt *selected, SzView *sections) {
  return sz_view_tabs(selected, sections);
}

SzView *sz_lang_view_card(SzView *child) { return sz_view_card(child); }

SzView *sz_lang_view_tooltip(SzString *message, SzView *child) {
  return sz_view_tooltip(message ? sz_string_cstr(message) : "", child);
}

SzView *sz_lang_view_on_secondary(SzView *child, SzViewTapFn tap, void *env) {
  return sz_view_on_secondary(child, tap, env);
}

SzView *sz_lang_view_focus_group(SzView *child) {
  return sz_view_focus_group(child);
}

SzView *sz_lang_view_placeholder(SzView *child) {
  return sz_view_placeholder(child);
}

SzView *sz_lang_view_semantics(SzString *label, SzView *child) {
  return sz_view_semantics(label ? sz_string_cstr(label) : "", child);
}

SzView *sz_lang_view_merge_semantics(SzString *label, SzView *child) {
  return sz_view_merge_semantics(label ? sz_string_cstr(label) : "", child);
}

SzView *sz_lang_view_ink_well(SzString *label, SzViewTapFn tap, void *env,
                             SzView *child) {
  return sz_view_ink_well(label ? sz_string_cstr(label) : "", tap, env, child);
}

SzView *sz_lang_view_visibility(SzSignalInt *sig, SzView *child) {
  return sz_view_visibility(sig, child);
}

SzView *sz_lang_view_offstage(SzSignalInt *sig, SzView *child) {
  return sz_view_offstage(sig, child);
}

SzView *sz_lang_view_unconstrained_box(SzView *child) {
  return sz_view_unconstrained_box(child);
}

SzView *sz_lang_view_divider(void) { return sz_view_divider(); }

SzView *sz_lang_view_expansion_tile(SzSignalInt *sig, SzString *title,
                                   SzView *child) {
  return sz_view_expansion_tile(sig, title ? sz_string_cstr(title) : "", child);
}

SzView *sz_lang_view_column(void) { return sz_view_column(); }

SzView *sz_lang_view_row(void) { return sz_view_row(); }

SzView *sz_lang_view_wrap(void) { return sz_view_wrap(); }
SzView *sz_lang_view_breadcrumb(void) { return sz_view_breadcrumb(); }
SzView *sz_lang_view_grid(int64_t cols) { return sz_view_grid((int)cols); }

SzView *sz_lang_view_stack(void) { return sz_view_stack(); }

SzView *sz_lang_view_each(SzSignalList *sig) { return sz_view_each(sig); }
SzView *sz_lang_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env) {
  return sz_view_each_map(sig, fn, env);
}

SzView *sz_lang_view_scroll(SzView *child) { return sz_view_scroll(child); }
SzView *sz_lang_view_scroll_h(SzView *child) { return sz_view_scroll_h(child); }
SzView *sz_lang_view_expanded(SzView *child) { return sz_view_expanded(child); }
SzView *sz_lang_view_center(SzView *child) { return sz_view_center(child); }
SzView *sz_lang_view_positioned(int64_t x, int64_t y, SzView *child) {
  return sz_view_positioned((int)x, (int)y, child);
}
SzView *sz_lang_view_padding(int64_t pad, SzView *child) {
  return sz_view_padding((int)pad, child);
}
SzView *sz_lang_view_sized(int64_t w, int64_t h, SzView *child) {
  return sz_view_sized((int)w, (int)h, child);
}
SzView *sz_lang_view_min_size(int64_t w, int64_t h, SzView *child) {
  return sz_view_min_size((int)w, (int)h, child);
}

SzView *sz_lang_view_max_size(int64_t w, int64_t h, SzView *child) {
  return sz_view_max_size((int)w, (int)h, child);
}
SzView *sz_lang_view_text_color(int64_t argb, SzView *child) {
  return sz_view_text_color((uint32_t)argb, child);
}
SzView *sz_lang_view_font_size(int64_t n, SzView *child) {
  return sz_view_font_size((int)n, child);
}
SzView *sz_lang_view_background(int64_t argb, SzView *child) {
  return sz_view_background((uint32_t)argb, child);
}
SzView *sz_lang_view_aspect_ratio(int64_t rw, int64_t rh, SzView *child) {
  return sz_view_aspect_ratio((int)rw, (int)rh, child);
}
SzView *sz_lang_view_fraction(int64_t wpct, int64_t hpct, SzView *child) {
  return sz_view_fraction((int)wpct, (int)hpct, child);
}

SzView *sz_lang_view_text_field(SzSignalStr *text, SzString *placeholder) {
  return sz_view_text_field(text, placeholder ? sz_string_cstr(placeholder) : "");
}

SzView *sz_lang_view_editor(SzSignalStr *text) {
  return sz_view_editor(text);
}

SzView *sz_lang_view_split(SzSignalInt *frac, SzView *start, SzView *end) {
  return sz_view_split(frac, start, end);
}

SzView *sz_lang_view_overlay(SzSignalInt *open, SzView *child) {
  return sz_view_overlay(open, child);
}

SzView *sz_lang_view_icon(int64_t glyph, int64_t argb) {
  return sz_view_icon((char)glyph, (uint32_t)argb);
}

SzView *sz_lang_view_image(int64_t w, int64_t h, int64_t argb, SzString *caption) {
  return sz_view_image((int)w, (int)h, (uint32_t)argb,
                       caption ? sz_string_cstr(caption) : "");
}

SzView *sz_lang_view_link(SzString *label, SzString *route) {
  return sz_view_link(label ? sz_string_cstr(label) : "",
                      route ? sz_string_cstr(route) : "");
}

SzView *sz_lang_view_nav_tile(int64_t glyph, SzString *title, SzString *route) {
  return sz_view_nav_tile((char)glyph, title ? sz_string_cstr(title) : "",
                          route ? sz_string_cstr(route) : "");
}

/* Codegen declares this as returning ptr: Scuzz Unit is a null pointer.
   Do not change the return type to void. */
void *sz_lang_view_add_child(SzView *parent, SzView *child) {
  sz_view_add_child(parent, child);
  return NULL;
}

SzView *sz_lang_view_show_when(SzSignalInt *sig, int64_t value, SzView *child) {
  return sz_view_show_when(sig, value, child);
}

SzView *sz_lang_view_bind_text(SzSignalStr *sig) { return sz_view_text_signal_str(sig); }

static int live_still_desktop(void) { return sz_embedder_alive(); }

static int live_still_mobile(void) { return sz_mobile_alive(); }

static int live_still_watch(void) { return 1; }

#ifdef __EMSCRIPTEN__
static SzUiSession *web_live_session;

static void web_live_frame(void) {
  SzUiSession *session = web_live_session;
  if (!session || !sz_ui_session_alive(session)) {
    emscripten_cancel_main_loop();
    sz_web_stop();
    web_live_session = NULL;
    return;
  }
  if (!sz_ui_session_needs_paint(session)) {
    sz_web_loop_pause();
    return;
  }
  if (!sz_ui_pump_sync(session)) {
    if (!sz_ui_session_alive(session)) {
      emscripten_cancel_main_loop();
      sz_web_stop();
      web_live_session = NULL;
      return;
    }
    sz_panic("Ui.run live pump failed");
  }
}
#endif

/* --- Ui.run -------------------------------------------------------------- */

typedef struct {
  SzUiRebuildFn rebuild;
  const char *schema;
} RebuildFnCell;

typedef struct {
  SzUiSession *session;
  int (*still)(void);
  int64_t frames;
  int64_t limit;
  int closing;
} RunCell;

static void *new_run(void *env) {
  RunCell *run = (RunCell *)sz_rc_alloc(sizeof *run, SZ_RC_BOX);
  memset(run, 0, sizeof *run);
  SzPair *owner = sz_pair_new(env, run);
  sz_release(run);
  return owner;
}

static void *thunk_run_rebuild(void *env) {
  SzPair *owner = env;
  SzPair *pack = owner->left;
  RunCell *run = owner->right;
  RebuildFnCell *cell = pack ? (RebuildFnCell *)pack->right : NULL;
  void *capture = pack ? pack->left : NULL;
  SzUiRebuildFn rebuild = cell ? cell->rebuild : NULL;
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *stamp;
  int inject_text = 0;

  fill_cfg(&cfg, 0, 0);

  if (!rebuild)
    sz_panic("Ui.run rebuild missing");
  root = rebuild(capture);
  if (!root)
    sz_panic("Ui.run rebuild returned null");

  session = sz_ui_mount(&cfg, root);
  if (!session)
    sz_panic("Ui.run mount failed");
  run->session = session;
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, rebuild, capture, cell->schema);
  stamp = getenv("SCUZZ_UI_RELOAD_STAMP");
  if (stamp && stamp[0])
    sz_ui_session_watch(session, stamp);
  {
    const char *dump = getenv("SCUZZ_UI_DEBUG_DUMP");
    if (dump && dump[0])
      sz_ui_session_set_debug_dump(session, dump);
  }
  {
    const char *inject = getenv("SCUZZ_UI_INJECT");
    if (inject && inject[0])
      sz_ui_session_set_inject(session, inject);
  }
  {
    const char *record = getenv("SCUZZ_UI_RECORD");
    if (record && record[0])
      sz_ui_session_set_record(session, record);
  }

#ifdef __EMSCRIPTEN__
  sz_web_start(session);
#endif
  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run pump failed");
  sz_testrt_session_baseline_snapshot();
  sz_signal_session_push();

  {
    const char *script = getenv("SCUZZ_UI_SCRIPT");
    if (script && script[0])
      sz_ui_script_run_file(session, script);
  }

  if (getenv("SCUZZ_UI_TAP")) {
    const char *seed = getenv("SCUZZ_UI_TEXT");
    if (seed && seed[0]) {
      SzInputEvent ev;
      memset(&ev, 0, sizeof(ev));
      ev.kind = SZ_INPUT_TEXT;
      ev.text = seed;
      if (!sz_ui_inject_sync(session, &ev))
        sz_panic("Ui.run text inject failed");
      inject_text = 1;
    }
    sz_ui_scripted_button_tap(session, inject_text);
  }

#ifdef __EMSCRIPTEN__
  web_live_session = session;
  sz_web_live_loop(web_live_frame);
  /* Keep the session mounted. The rAF loop owns it after main returns. */
  return NULL;
#endif
  run->session = session;
  if (cfg.kind == SZ_UI_RUNTIME_DESKTOP && sz_embedder_available())
    run->still = live_still_desktop;
  else if (stamp && stamp[0])
    run->still = live_still_watch;
  else if (cfg.kind == SZ_UI_RUNTIME_MOBILE && sz_mobile_available())
    run->still = live_still_mobile;
  else if (cfg.kind == SZ_UI_RUNTIME_HEADLESS && getenv("SCUZZ_UI_SERVE"))
    /* Daemon headless: pump until a quit op or signal. scuzz run sets
     * SCUZZ_UI_SERVE only without --exec, so fuzz and batch stay one-shot. */
    run->still = live_still_watch;
  const char *limit = getenv("SCUZZ_LIVE_FRAMES");
  run->limit = limit ? atoll(limit) : 0;
  return NULL;
}

static void *finish_run(void *env) {
  RunCell *run = env;
  if (!run || !run->session) return NULL;
  sz_io_ui_reap();
  if (sz_ui_session_alive(run->session)) sz_ui_pump_sync(run->session);
  if (sz_testrt_oracles_armed() &&
      sz_ui_quiesce(run->session) == SZ_QUIESCE_BUDGET_TRIPPED)
    sz_panic("quiesce budget tripped (64 pumps): timeline not settled");
  sz_ui_session_finish(run->session);
  sz_ui_unmount(run->session);
  run->session = NULL;
  /* The tree is gone. Record a trailing effect, then put signal values back. */
  sz_property_session_flush();
  sz_signal_session_pop();
  sz_testrt_session_baseline_check();
  return NULL;
}

static SzIo *pump_run(void *unused, void *env) {
  (void)unused;
  RunCell *run = env;
  sz_io_ui_reap();
  int alive = sz_ui_session_alive(run->session);
  if (!alive || (run->still && (!run->still() ||
                    (run->limit > 0 && run->frames >= run->limit)))) {
    if (!run->closing) sz_io_ui_cancel();
    run->closing = 1;
  }
  if ((run->closing || !run->still) && !sz_io_ui_pending())
    return sz_io_pure(NULL);
  if (alive && !run->closing) {
    if (!sz_ui_pump_sync(run->session) && sz_ui_session_alive(run->session))
      sz_panic("Ui.run live pump failed");
    run->frames++;
  }
  SzIo *sleep = sz_io_sleep_ms(run->still ? 16 : 1);
  SzIo *next = sz_io_flatmap(sleep, pump_run, run);
  sz_release(sleep);
  return next;
}

static void *cancel_handlers(void *env) {
  (void)env;
  sz_io_ui_cancel();
  return NULL;
}
static SzIo *finish_handlers(void *unused, void *env) {
  (void)unused;
  if (!sz_io_ui_pending()) return sz_io_delay(finish_run, env);
  SzIo *sleep = sz_io_sleep_ms(1);
  SzIo *next = sz_io_flatmap(sleep, finish_handlers, env);
  sz_release(sleep);
  return next;
}

static SzIo *mounted_run(void *value, void *env) {
  (void)env;
#ifdef __EMSCRIPTEN__
  return sz_io_delay(thunk_run_rebuild, value);
#else
  SzPair *owner = value;
  RunCell *run = owner->right;
  SzIo *mount = sz_io_delay(thunk_run_rebuild, owner);
  SzIo *inner = sz_io_flatmap(mount, pump_run, run);
  sz_release(mount);
  SzIo *cancel = sz_io_delay(cancel_handlers, run);
  SzIo *finish = sz_io_flatmap(cancel, finish_handlers, run);
  sz_release(cancel);
  SzIo *io = sz_io_ensure(inner, finish);
  sz_release(inner);
  sz_release(finish);
  return io;
#endif
}

SzIo *sz_ui_run_rebuild(SzUiRebuildFn fn, void *env, const char *schema) {
  RebuildFnCell *cell = (RebuildFnCell *)sz_rc_alloc(sizeof(RebuildFnCell), SZ_RC_BOX);
  cell->rebuild = fn;
  cell->schema = schema;
  SzPair *pack = sz_pair_new(env, cell);
  sz_release(cell);
  SzIo *mount = sz_io_delay(new_run, pack);
  sz_release(pack);
  SzIo *io = sz_io_flatmap(mount, mounted_run, NULL);
  sz_release(mount);
  return io;
}

SzView *sz_lang_view_code(SzString *text) {
  return sz_view_code(sz_string_cstr(text));
}
SzView *sz_lang_view_heading(int64_t level, SzView *child) {
  if (level < 1 || level > 6) sz_panic("Heading level must be 1 through 6");
  return sz_view_heading((int)level, child);
}
