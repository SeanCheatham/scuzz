#include "scuzz_ui.h"

SzView *sz_ui_reload_rebuild(void *env) {
  SzSignalInt *count = (SzSignalInt *)env;
  SzView *root = sz_view_column();
  sz_view_add_child(root, sz_view_text("B"));
  sz_view_add_child(root, sz_view_text_signal_int(count, "n="));
  return root;
}
