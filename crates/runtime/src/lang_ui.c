#define _POSIX_C_SOURCE 200809L

#include "scuzz_ui.h"
#include "scuzz_embedder.h"
#include "scuzz_mobile.h"

#include "ui_script.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fill_cfg(SzUiConfig *cfg, int width, int height) {
  double scale = 1.0;
  const char *rt = getenv("SCUZZ_UI_RUNTIME");
  memset(cfg, 0, sizeof(*cfg));
  cfg->kind = SZ_UI_RUNTIME_HEADLESS;
  if (rt && (strcmp(rt, "desktop") == 0 || strcmp(rt, "Desktop") == 0))
    cfg->kind = SZ_UI_RUNTIME_DESKTOP;
  else if (rt && (strcmp(rt, "mobile") == 0 || strcmp(rt, "Mobile") == 0))
    cfg->kind = SZ_UI_RUNTIME_MOBILE;
  cfg->width = width;
  cfg->height = height;
  cfg->title = sz_ui_default_title();
  sz_ui_resolve_headless_size(&cfg->width, &cfg->height, &scale);
  cfg->scale = scale;
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
SzView *sz_lang_view_grid(int64_t cols) { return sz_view_grid((int)cols); }

SzView *sz_lang_view_stack(void) { return sz_view_stack(); }

SzView *sz_lang_view_each(SzSignalList *sig) { return sz_view_each(sig); }
SzView *sz_lang_view_each_map(SzSignalList *sig, SzViewEachFn fn, void *env) {
  return sz_view_each_map(sig, fn, env);
}

SzView *sz_lang_view_scroll(SzView *child) { return sz_view_scroll(child); }
SzView *sz_lang_view_scroll_h(SzView *child) { return sz_view_scroll_h(child); }
SzView *sz_lang_view_expanded(SzView *child) { return sz_view_expanded(child); }
SzView *sz_lang_view_stretch(SzView *child) { return sz_view_stretch(child); }
SzView *sz_lang_view_center(SzView *child) { return sz_view_center(child); }
SzView *sz_lang_view_align(int64_t ax, int64_t ay, SzView *child) {
  return sz_view_align((int)ax, (int)ay, child);
}
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
SzView *sz_lang_view_clip(SzView *child) { return sz_view_clip(child); }
SzView *sz_lang_view_opacity(int64_t pct, SzView *child) {
  return sz_view_opacity((int)pct, child);
}
SzView *sz_lang_view_max_lines(int64_t n, SzView *child) {
  return sz_view_max_lines((int)n, child);
}
SzView *sz_lang_view_ignore_pointer(SzView *child) {
  return sz_view_ignore_pointer(child);
}
SzView *sz_lang_view_absorb_pointer(SzView *child) {
  return sz_view_absorb_pointer(child);
}
SzView *sz_lang_view_exclude_semantics(SzView *child) {
  return sz_view_exclude_semantics(child);
}
SzView *sz_lang_view_ellipsis(SzView *child) { return sz_view_ellipsis(child); }
SzView *sz_lang_view_text_color(int64_t argb, SzView *child) {
  return sz_view_text_color((uint32_t)argb, child);
}
SzView *sz_lang_view_gap(int64_t n, SzView *child) {
  return sz_view_gap((int)n, child);
}
SzView *sz_lang_view_font_size(int64_t n, SzView *child) {
  return sz_view_font_size((int)n, child);
}
SzView *sz_lang_view_border(int64_t n, int64_t argb, SzView *child) {
  return sz_view_border((int)n, (uint32_t)argb, child);
}
SzView *sz_lang_view_radius(int64_t n, SzView *child) {
  return sz_view_radius((int)n, child);
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

static void live_pump_loop(SzUiSession *session, int (*still)(void)) {
  const char *max_frames_env = getenv("SCUZZ_LIVE_FRAMES");
  int64_t max_frames =
      (max_frames_env && atoi(max_frames_env) > 0) ? atoi(max_frames_env) : 0;
  int64_t frame = 0;
  while (sz_ui_session_alive(session) && still()) {
    if (!sz_ui_pump_sync(session)) {
      if (!sz_ui_session_alive(session))
        break;
      sz_panic("Ui.run live pump failed");
    }
    if (!sz_ui_session_alive(session))
      break;
    frame++;
    if (max_frames > 0 && frame >= max_frames)
      break;
    {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 16000000L; /* ~60fps cap */
      nanosleep(&ts, NULL);
    }
  }
}

/* --- Ui.run -------------------------------------------------------------- */

typedef struct {
  SzUiRebuildFn rebuild;
} RebuildFnCell;

static void *thunk_run_rebuild(void *env) {
  SzPair *pack = (SzPair *)env;
  RebuildFnCell *cell = pack ? (RebuildFnCell *)pack->right : NULL;
  void *capture = pack ? pack->left : NULL;
  SzUiRebuildFn rebuild = cell ? cell->rebuild : NULL;
  SzUiConfig cfg;
  SzUiSession *session;
  SzView *root;
  const char *stamp;
  int interactive;
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
  sz_ui_session_take_root(session);
  sz_ui_session_set_rebuild(session, rebuild, capture);
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

  if (!sz_ui_pump_sync(session))
    sz_panic("Ui.run pump failed");
  sz_testrt_session_baseline_snapshot();

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

  interactive = cfg.kind == SZ_UI_RUNTIME_DESKTOP && sz_embedder_available();
  if (interactive) {
    live_pump_loop(session, live_still_desktop);
    sz_ui_session_finish(session);
  } else if (stamp && stamp[0]) {
    live_pump_loop(session, live_still_watch);
    sz_ui_session_finish(session);
  } else if (cfg.kind == SZ_UI_RUNTIME_MOBILE && sz_mobile_available()) {
    /* Host shell reports alive=0, so CI stays one frame. */
    live_pump_loop(session, live_still_mobile);
    sz_ui_session_finish(session);
  } else {
    if (sz_testrt_oracles_armed() &&
        sz_ui_quiesce(session) == SZ_QUIESCE_BUDGET_TRIPPED)
      sz_panic("quiesce budget tripped (64 pumps): timeline not settled");
    sz_ui_session_finish(session);
  }

  /* Check after unmount: view-owned capture packs pin session values until
     teardown, so a pre-unmount check cannot tell session state from leaks. */
  sz_ui_unmount(session);
  sz_testrt_session_baseline_check();
  return NULL;
}

SzIo *sz_ui_run_rebuild(SzUiRebuildFn fn, void *env) {
  RebuildFnCell *cell = (RebuildFnCell *)sz_rc_alloc(sizeof(RebuildFnCell), SZ_RC_BOX);
  SzPair *pack;
  cell->rebuild = fn;
  pack = sz_pair_new(env, cell);
  sz_release(cell);
  {
    SzIo *io = sz_io_delay(thunk_run_rebuild, pack);
    sz_release(pack);
    return io;
  }
}
