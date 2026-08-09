/* Android JNI packaging shell (Phase 5).
 *
 * Maps MotionEvent / lifecycle / InputConnection onto SuInputEvent and drives
 * the same UiSession protocol as Headless / Window / host mobile shell.
 *
 * This file is a compile-shaped stub: it includes the C ABI and shows the
 * mount → inject → pump → present loop. Real JNI glue is filled when an NDK
 * toolchain is available.
 */

#include "scalui_mobile.h"
#include "scalui_ui.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  SuUiSession *session;
  SuView *root;
} ScaluiAndroidApp;

static ScaluiAndroidApp g_app;

int scalui_android_mount(SuView *root, int width, int height) {
  SuUiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SU_UI_RUNTIME_MOBILE;
  cfg.width = width > 0 ? width : 360;
  cfg.height = height > 0 ? height : 640;
  cfg.scale = 1.0;
  cfg.title = "ScalUI";
  g_app.root = root;
  g_app.session = su_ui_mount(&cfg, root);
  if (!g_app.session)
    return 0;
  su_ui_session_take_root(g_app.session);
  return su_ui_pump_sync(g_app.session);
}

int scalui_android_touch(int phase, float x, float y) {
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

int scalui_android_lifecycle(int phase) {
  SuInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_LIFECYCLE;
  ev.lifecycle = (SuLifecyclePhase)phase;
  return su_ui_inject_sync(g_app.session, &ev);
}

int scalui_android_text(const char *text) {
  SuInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SU_INPUT_TEXT;
  ev.text = text;
  return su_ui_inject_sync(g_app.session, &ev) && su_ui_pump_sync(g_app.session);
}

void scalui_android_unmount(void) {
  if (g_app.session) {
    su_ui_unmount(g_app.session);
    g_app.session = NULL;
    g_app.root = NULL;
  }
}

/* Strong mobile present for on-device shells: pixels are handed to a Surface
 * View by the Java side after peeking via a host-provided callback. Until the
 * NDK link step, prefer the host shell simulator on Linux CI. */
