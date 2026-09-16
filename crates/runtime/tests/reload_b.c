#include "scuzz_ui.h"

#ifndef RELOAD_SCHEMA
#define RELOAD_SCHEMA "counter:int"
#endif
const char sz_ui_reload_capture[] = RELOAD_SCHEMA;

SzView *sz_ui_reload_rebuild(void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  SzView *root = sz_view_column();
  sz_view_add_child(root, sz_view_text("B"));
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  return root;
}
