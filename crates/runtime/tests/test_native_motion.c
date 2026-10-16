#define _POSIX_C_SOURCE 200809L
#include "scuzz_ui.h"
#include "scuzz_embedder.h"
#include "scuzz_mobile.h"
#include "../src/ui_script.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int32_t sz_eval_value_tag_base = 42;
int sz_ui_session_live_inject(SzUiSession *session, const SzInputEvent *event);

static int preference, queued, queries, presents, shutdowns;
int sz_embedder_available(void) { return 1; }
double sz_embedder_display_scale(void) { return 1; }
int sz_embedder_reduced_motion(void) { queries++; return preference; }
int sz_embedder_poll_event(SzInputEvent *out) {
  if (!queued) return 0;
  queued = 0;
  *out = (SzInputEvent){.kind = SZ_INPUT_REDUCED_MOTION, .reduced_motion = preference};
  return 1;
}
int sz_embedder_present(const char *title, int w, int h, int pw, int ph,
                        const uint8_t *rgba, size_t bytes) {
  assert(title && w > 0 && h > 0 && pw > 0 && ph > 0 && rgba && bytes);
  presents++;
  return 1;
}
void sz_embedder_shutdown(void) { queued = 0; shutdowns++; }

int sz_mobile_available(void) { return 1; }
int sz_mobile_reduced_motion(void) { return sz_embedder_reduced_motion(); }
int sz_mobile_poll_event(SzInputEvent *out) { return sz_embedder_poll_event(out); }
int sz_mobile_present(const char *title, int w, int h, int pw, int ph,
                      const uint8_t *rgba, size_t bytes) {
  return sz_embedder_present(title, w, h, pw, ph, rgba, bytes);
}
void sz_mobile_shutdown(void) { sz_embedder_shutdown(); }

typedef struct Fixture {
  SzSignalInt *value;
  SzSignalStr *input, *label;
  SzView *root, *action, *field;
} Fixture;

static void change(SzView *view, void *env) {
  (void)view;
  Fixture *f = env;
  int next = sz_signal_int_get(f->value) == 20 ? 80 : 20;
  sz_signal_int_set(f->value, next);
  sz_signal_str_set(f->label, next == 20 ? "short" :
      "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight");
}

static void fixture_make(Fixture *f, int kind) {
  f->value = sz_signal_int(20);
  f->input = sz_signal_str("Retained input");
  f->label = sz_signal_str("short");
  f->action = sz_view_button("Change", change, f);
  f->field = sz_view_text_field(f->input, "Name");
  SzView *motion = NULL;
  switch (kind) {
  case 0: motion = sz_view_animated_opacity(f->value, 8, f->field); break;
  case 1: motion = sz_view_animated_size(f->value, f->value, 8, f->field); break;
  case 2: motion = sz_view_animated_position(f->value, f->value, 8, f->field); break;
  case 3: motion = sz_view_animated_scale(f->value, 8, f->field); break;
  case 4: {
    SzView *body = sz_view_column();
    sz_view_add_child(body, f->field);
    sz_view_add_child(body, sz_view_text_signal_str(f->label));
    motion = sz_view_animated_content_size(8, body);
    break;
  }
  default: assert(!"unknown transition");
  }
  f->root = sz_view_column();
  sz_view_add_child(f->root, f->action);
  sz_view_add_child(f->root, motion);
}

static void fixture_free(Fixture *f) {
  sz_view_free(f->root);
  sz_signal_int_free(f->value);
  sz_signal_str_free(f->input);
  sz_signal_str_free(f->label);
}

static void prepare(Fixture *f) {
  sz_view_focus(f->root, f->action);
  sz_view_set_text_field_sel(f->field, 1, 4);
}

int main(void) {
  const char *path = "/tmp/scuzz-native-motion.json";
  unsetenv("SCUZZ_UI_REDUCED_MOTION");
  for (int peer = 0; peer <= 1; peer++)
      for (int kind = 0; kind < 5; kind++) for (int initial = 0; initial <= 1; initial++)
      for (int force = 0; force <= 1; force++)
      for (int steps = 1; steps <= 5; steps++) for (int cut = 0; cut <= 1; cut++) {
    Fixture live, replay;
    if (force) setenv("SCUZZ_UI_REDUCED_MOTION", "1", 1);
    else unsetenv("SCUZZ_UI_REDUCED_MOTION");
    preference = initial;
    int before_queries = queries, before_shutdowns = shutdowns;
    SzUiConfig cfg = {.kind = peer ? SZ_UI_RUNTIME_MOBILE : SZ_UI_RUNTIME_DESKTOP, .width = 320,
                     .height = 280, .scale = 1, .reduced_motion = force};
    fixture_make(&live, kind);
    SzUiSession *session = sz_ui_mount(&cfg, live.root);
    assert(session && sz_ui_pump_sync(session));
    assert(queries == before_queries + 1);
    prepare(&live);
    assert(sz_ui_session_set_record(session, path));
    for (int i = 0; i < steps; i++) {
      SzInputEvent key = {.kind = SZ_INPUT_KEY, .key = "Space"};
      assert(sz_ui_session_live_inject(session, &key));
      assert(sz_ui_pump_sync(session));
      assert(sz_view_transition_pending(live.root) == !(preference || force));
      if (cut && i + 1 == steps) break;
      preference = !preference;
      queued = 1;
      assert(sz_ui_pump_sync(session));
      assert(!queued);
      if (preference || force) assert(!sz_view_motion_pending(live.root));
    }
    assert(!strcmp(sz_signal_str_get(live.input), "Retained input"));
    assert(sz_view_text_field_sel_start(live.field) == 1);
    assert(sz_view_text_field_sel_end(live.field) == 4);
    SzString *expected = sz_view_a11y_dump(live.root);
    int pending = sz_view_motion_pending(live.root);
    sz_ui_unmount(session);
    assert(shutdowns == before_shutdowns + 1);
    int before_presents = presents;
    cfg.kind = SZ_UI_RUNTIME_HEADLESS;
    cfg.reduced_motion = !(initial || force);
    fixture_make(&replay, kind);
    session = sz_ui_mount(&cfg, replay.root);
    assert(session && sz_ui_pump_sync(session));
    prepare(&replay);
    sz_ui_script_run_file(session, path);
    assert(queries == before_queries + 1 && presents == before_presents);
    assert(sz_signal_int_get(live.value) == sz_signal_int_get(replay.value));
    assert(!strcmp(sz_signal_str_get(live.label), sz_signal_str_get(replay.label)));
    assert(!strcmp(sz_signal_str_get(replay.input), "Retained input"));
    assert(sz_view_text_field_sel_start(replay.field) == 1);
    assert(sz_view_text_field_sel_end(replay.field) == 4);
    assert(sz_view_motion_pending(replay.root) == pending);
    SzString *actual = sz_view_a11y_dump(replay.root);
    if (strcmp(sz_string_cstr(expected), sz_string_cstr(actual)))
      fprintf(stderr, "transition=%d initial=%d steps=%d\nexpected=%s\nactual=%s\n",
              kind, initial, steps, sz_string_cstr(expected), sz_string_cstr(actual));
    assert(!strcmp(sz_string_cstr(expected), sz_string_cstr(actual)));
    sz_string_free(expected); sz_string_free(actual);
    sz_ui_unmount(session);
    assert(shutdowns == before_shutdowns + 1);
    fixture_free(&live); fixture_free(&replay);
  }
  remove(path);
  puts("Native motion: 400 recordings replay with equal transitions and retained input.");
  return 0;
}
