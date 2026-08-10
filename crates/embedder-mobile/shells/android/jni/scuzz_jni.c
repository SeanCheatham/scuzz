/* Android JNI packaging shell.
 *
 * Maps MotionEvent / lifecycle / InputConnection onto SzInputEvent and drives
 * the same UiSession protocol as Headless / Window / host mobile shell.
 *
 * This file is a compile-shaped stub: it includes the C ABI and shows the
 * mount → inject → pump → present loop. Real JNI glue is filled when an NDK
 * toolchain is available.
 */

#include "scuzz_mobile.h"
#include "scuzz_ui.h"

#include <stdint.h>
#include <string.h>

typedef struct {
  SzUiSession *session;
  SzView *root;
} ScuzzAndroidApp;

static ScuzzAndroidApp g_app;

int scuzz_android_mount(SzView *root, int width, int height) {
  SzUiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.kind = SZ_UI_RUNTIME_MOBILE;
  cfg.width = width > 0 ? width : 360;
  cfg.height = height > 0 ? height : 640;
  cfg.scale = 1.0;
  cfg.title = "Scuzz Lang";
  g_app.root = root;
  g_app.session = sz_ui_mount(&cfg, root);
  if (!g_app.session)
    return 0;
  sz_ui_session_take_root(g_app.session);
  return sz_ui_pump_sync(g_app.session);
}

int scuzz_android_touch(int phase, float x, float y) {
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

int scuzz_android_lifecycle(int phase) {
  SzInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = (SzLifecyclePhase)phase;
  return sz_ui_inject_sync(g_app.session, &ev);
}

int scuzz_android_text(const char *text) {
  SzInputEvent ev;
  if (!g_app.session)
    return 0;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT;
  ev.text = text;
  return sz_ui_inject_sync(g_app.session, &ev) && sz_ui_pump_sync(g_app.session);
}

void scuzz_android_unmount(void) {
  if (g_app.session) {
    sz_ui_unmount(g_app.session);
    g_app.session = NULL;
    g_app.root = NULL;
  }
}

/* Strong mobile present for on-device shells: pixels are handed to a Surface
 * View by the Java side after peeking via a host-provided callback. Until the
 * NDK link step, prefer the host shell simulator on Linux CI. */
