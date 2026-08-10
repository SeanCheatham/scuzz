/* iOS packaging shell.
 *
 * UIApplicationDelegate drives mount → pump; UITouch / lifecycle map to
 * SzInputEvent. Compile-shaped stub — link libscuzz_rt + libsk_capi under
 * Xcode for a device/simulator binary.
 */

#include "scuzz_mobile.h"
#include "scuzz_ui.h"

#include <string.h>

typedef struct {
  SzUiSession *session;
} ScuzzIosApp;

static ScuzzIosApp g_app;

int scuzz_ios_mount(SzView *root, int width, int height) {
  SzUiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_MOBILE;
  cfg.width = width > 0 ? width : 390;
  cfg.height = height > 0 ? height : 844;
  cfg.scale = 1.0;
  cfg.title = "Scuzz Lang";
  g_app.session = sz_ui_mount(&cfg, root);
  if (!g_app.session)
    return 0;
  sz_ui_session_take_root(g_app.session);
  return sz_ui_pump_sync(g_app.session);
}

int scuzz_ios_touch(int phase, float x, float y) {
  SzInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = (SzPointerPhase)phase;
  ev.x = x;
  ev.y = y;
  return sz_ui_inject_sync(g_app.session, &ev) && sz_ui_pump_sync(g_app.session);
}

int scuzz_ios_lifecycle(int phase) {
  SzInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = (SzLifecyclePhase)phase;
  return sz_ui_inject_sync(g_app.session, &ev);
}

void scuzz_ios_unmount(void) {
  if (g_app.session) {
    sz_ui_unmount(g_app.session);
    g_app.session = NULL;
  }
}
