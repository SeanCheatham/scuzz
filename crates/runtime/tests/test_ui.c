#include "scalui_ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int files_equal(const char *a, const char *b) {
  FILE *fa = fopen(a, "rb");
  FILE *fb = fopen(b, "rb");
  unsigned char ba[4096], bb[4096];
  size_t na, nb;
  if (!fa || !fb) {
    if (fa)
      fclose(fa);
    if (fb)
      fclose(fb);
    return 0;
  }
  for (;;) {
    na = fread(ba, 1, sizeof ba, fa);
    nb = fread(bb, 1, sizeof bb, fb);
    if (na != nb || memcmp(ba, bb, na) != 0) {
      fclose(fa);
      fclose(fb);
      return 0;
    }
    if (na == 0)
      break;
  }
  fclose(fa);
  fclose(fb);
  return 1;
}

int main(void) {
  SuUiConfig cfg;
  SuView *view;
  SuUiSession *session;
  SuInputEvent tap;
  const char *path_a = "/tmp/scalui_ui_a.png";
  const char *path_b = "/tmp/scalui_ui_b.png";
  const char *path_tap = "/tmp/scalui_ui_tap.png";
  uint8_t *bytes = NULL;
  size_t len = 0;

  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_HEADLESS;
  cfg.width = 200;
  cfg.height = 100;
  cfg.scale = 1.0;

  view = su_view_label("Hello", 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_HEADLESS);
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_a));
  assert(su_ui_snapshot_png_bytes(session, &bytes, &len));
  assert(bytes && len > 8 && bytes[0] == 137);
  free(bytes);

  /* Deterministic: second snapshot matches */
  assert(su_ui_snapshot_png_sync(session, path_b));
  assert(files_equal(path_a, path_b));

  memset(&tap, 0, sizeof(tap));
  tap.kind = SU_INPUT_TAP;
  tap.x = 100;
  tap.y = 50;
  assert(su_ui_inject_sync(session, &tap));
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_tap));
  assert(!files_equal(path_a, path_tap));

  su_ui_unmount(session);
  su_view_free(view);

  /* Window peer shares protocol */
  cfg.kind = SU_UI_RUNTIME_WINDOW;
  cfg.title = "test";
  view = su_view_label("Win", 0xFF142850u, 0xFFF0F0F0u);
  session = su_ui_mount(&cfg, view);
  assert(session);
  assert(su_ui_session_kind(session) == SU_UI_RUNTIME_WINDOW);
  assert(su_ui_pump_sync(session));
  assert(su_ui_snapshot_png_sync(session, path_b));
  su_ui_unmount(session);
  su_view_free(view);

  /* IO-shaped headless demo */
  {
    SuIoResult r = su_io_unsafe_run(su_ui_run_headless_label("Demo", 160, 80));
    assert(r.ok);
  }

  remove(path_a);
  remove(path_b);
  remove(path_tap);
  puts("runtime ui tests ok");
  return 0;
}
