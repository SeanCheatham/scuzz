/* iOS packaging shell (Phase 5).
 *
 * UIApplicationDelegate drives mount → pump; UITouch / lifecycle map to
 * SuInputEvent. Compile-shaped stub — link libscalui_rt + libsk_capi under
 * Xcode for a device/simulator binary.
 */

#include "scalui_mobile.h"
#include "scalui_ui.h"

#include <string.h>

typedef struct {
  SuUiSession *session;
} ScaluiIosApp;

static ScaluiIosApp g_app;

int scalui_ios_mount(SuView *root, int width, int height) {
  SuUiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_MOBILE;
  cfg.width = width > 0 ? width : 390;
  cfg.height = height > 0 ? height : 844;
  cfg.scale = 1.0;
  cfg.title = "ScalUI";
  g_app.session = su_ui_mount(&cfg, root);
  if (!g_app.session)
    return 0;
  su_ui_session_take_root(g_app.session);
  return su_ui_pump_sync(g_app.session);
}

int scalui_ios_touch(int phase, float x, float y) {
  SuInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_POINTER;
  ev.pointer_phase = (SuPointerPhase)phase;
  ev.x = x;
  ev.y = y;
  return su_ui_inject_sync(g_app.session, &ev) && su_ui_pump_sync(g_app.session);
}

int scalui_ios_lifecycle(int phase) {
  SuInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_LIFECYCLE;
  ev.lifecycle = (SuLifecyclePhase)phase;
  return su_ui_inject_sync(g_app.session, &ev);
}

void scalui_ios_unmount(void) {
  if (g_app.session) {
    su_ui_unmount(g_app.session);
    g_app.session = NULL;
  }
}
