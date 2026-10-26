#define _POSIX_C_SOURCE 200809L
#include "scuzz_mobile.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int scuzz_android_push_text_edit(const char *text);
int scuzz_android_push_accessibility(uintptr_t id, int action, const char *text,
                                     int start, int end, int64_t value);
char *scuzz_android_accessibility_snapshot(uint64_t previous);
void scuzz_android_set_alive(int alive);

static void increment(SzView *view, void *context) {
  (void)view;
  SzSignalInt *count = context;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
}

static uint64_t version(const char *document) {
  unsigned long long result;
  assert(document && sscanf(document, "{\"version\":%llu,", &result) == 1);
  return (uint64_t)result;
}

static void queue_ownership(void) {
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i < 63; i++) {
      char text[64];
      snprintf(text, sizeof text, "%d:%d:é😀", cycle, i);
      assert(i % 2 ? scuzz_android_push_text_edit(text) :
        scuzz_android_push_accessibility((uintptr_t)i + 1, SZ_A11Y_SET_TEXT, text, 0, 0, 0));
      memset(text, 'x', strlen(text));
    }
    assert(!scuzz_android_push_text_edit("full"));
    for (int i = 0; i < 63; i++) {
      char text[64];
      SzInputEvent event;
      snprintf(text, sizeof text, "%d:%d:é😀", cycle, i);
      assert(sz_mobile_poll_event(&event));
      assert(event.kind == (i % 2 ? SZ_INPUT_TEXT_EDIT : SZ_INPUT_ACCESSIBILITY));
      assert(!strcmp(event.text, text));
    }
    SzInputEvent event;
    assert(!sz_mobile_poll_event(&event));
  }
}

static atomic_int reader_stop, reader_ready;

static void *read_snapshots(void *unused) {
  (void)unused;
  do {
    char *document = scuzz_android_accessibility_snapshot(UINT64_MAX);
    assert(document && version(document) > 0);
    assert(strstr(document, "\"nodes\":["));
    free(document);
    atomic_store(&reader_ready, 1);
  } while (!atomic_load(&reader_stop));
  return NULL;
}

int main(void) {
  queue_ownership();
  SzSignalInt *count = sz_signal_int(0);
  SzSignalStr *text = sz_signal_str("é😀\"\\\n");
  SzView *root = sz_view_column();
  SzView *button = sz_view_button("Same", increment, count);
  uintptr_t id = sz_view_id(button);
  sz_view_add_child(root, button);
  SzView *field = sz_view_text_field(text, "Draft");
  sz_view_add_child(root, field);
  SzUiConfig config = {.kind = SZ_UI_RUNTIME_MOBILE, .width = 240, .height = 160, .scale = 1};
  scuzz_android_set_alive(1);
  SzUiSession *session = sz_ui_mount(&config, root);
  assert(session);
  sz_ui_session_take_root(session);
  assert(sz_ui_pump_sync(session));
  char *first = scuzz_android_accessibility_snapshot(UINT64_MAX);
  assert(first && strstr(first, "é😀\\\"\\\\\\u000a"));
  assert(strstr(first, "\"label\":\"Draft\""));
  assert(!scuzz_android_accessibility_snapshot(version(first)));
  sz_mobile_publish_accessibility(sz_ui_session_a11y_snapshot(session), 240, 160);
  assert(!scuzz_android_accessibility_snapshot(version(first)));

  assert(scuzz_android_push_accessibility(id, SZ_A11Y_ACTIVATE, NULL, 0, 0, 0));
  assert(scuzz_android_push_accessibility(sz_view_id(field), SZ_A11Y_SET_TEXT, "é😀z", 0, 0, 0));
  assert(scuzz_android_push_accessibility(sz_view_id(field), SZ_A11Y_SET_SELECTION, NULL, 2, 6, 0));
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);
  assert(!strcmp(sz_signal_str_get(text), "é😀z"));
  assert(sz_view_text_field_sel_start(field) == 2 && sz_view_text_field_sel_end(field) == 6);
  char *edited = scuzz_android_accessibility_snapshot(version(first));
  assert(edited && strstr(edited, "\"value\":\"é😀z\""));
  assert(strstr(edited, "\"start\":2,\"end\":6"));
  free(edited);

  pthread_t reader;
  assert(!pthread_create(&reader, NULL, read_snapshots, NULL));
  while (!atomic_load(&reader_ready)) {}
  for (int i = 0; i < 100; i++) {
    char value[64];
    snprintf(value, sizeof value, "%d:é😀", i);
    sz_signal_str_set(text, value);
    sz_mobile_publish_accessibility(sz_ui_session_a11y_snapshot(session), 240, 160);
  }
  atomic_store(&reader_stop, 1);
  assert(!pthread_join(reader, NULL));

  assert(scuzz_android_push_accessibility(id, SZ_A11Y_ACTIVATE, NULL, 0, 0, 0));
  sz_view_clear_children(root);
  button = sz_view_button("Same", increment, count);
  assert(sz_view_id(button) != id);
  sz_view_add_child(root, button);
  assert(sz_ui_pump_sync(session));
  assert(sz_signal_int_get(count) == 1);
  sz_ui_unmount(session);
  assert(strstr(first, "é😀\\\"\\\\\\u000a"));
  free(first);
  char *empty = scuzz_android_accessibility_snapshot(UINT64_MAX);
  assert(empty && strstr(empty, "\"nodes\":[]"));
  free(empty);
  SzInputEvent event;
  assert(!sz_mobile_poll_event(&event));
  sz_signal_str_free(text);
  sz_signal_int_free(count);
  puts("Android accessibility transport tests pass");
  return 0;
}
