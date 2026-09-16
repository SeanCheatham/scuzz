#include "scuzz_ui.h"

const char sz_ui_reload_capture[] = "counter:int";

extern SzIo *scuzz_reload_later;

static void *later_apply(void *env) {
  SzSignalInt *count = ((SzPair *)env)->left;
  sz_signal_int_set(count, sz_signal_int_get(count) + 1);
  return NULL;
}

SzView *sz_ui_reload_rebuild(void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  SzView *root = sz_view_column();
  sz_view_add_child(root, sz_view_text("A"));
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  sz_release(scuzz_reload_later);
  SzPair *capture = sz_pair_new(count, NULL);
  scuzz_reload_later = sz_io_delay(later_apply, capture);
  sz_release(capture);
  return root;
}
