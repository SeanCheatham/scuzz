#ifndef SCALUI_UI_H
#define SCALUI_UI_H

#include "scalui_rt.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UiRuntime peer interpreters (ADR 0004). Headless is first-class. */
typedef enum SuUiRuntimeKind {
  SU_UI_RUNTIME_HEADLESS = 1,
  SU_UI_RUNTIME_WINDOW = 2
} SuUiRuntimeKind;

typedef struct SuUiConfig {
  SuUiRuntimeKind kind;
  int width;
  int height;
  double scale;      /* Phase 1: recorded; raster uses logical pixels */
  const char *title; /* Window only; may be NULL */
} SuUiConfig;

typedef enum SuInputKind {
  SU_INPUT_TAP = 1,
  SU_INPUT_RESIZE = 2
} SuInputKind;

typedef struct SuInputEvent {
  SuInputKind kind;
  float x;
  float y;
  int width;  /* resize */
  int height; /* resize */
} SuInputEvent;

typedef struct SuView SuView;
typedef struct SuUiSession SuUiSession;

/* Phase 1 minimal Views — Phase 2 adds the real declarative tree. */
SuView *su_view_label(const char *text, uint32_t bg_argb, uint32_t fg_argb);
void su_view_free(SuView *view);

/* Mount root under a runtime. Window shares the session protocol; Phase 1
 * presents offscreen (no OS window yet — see crates/embedder-desktop). */
SuUiSession *su_ui_mount(const SuUiConfig *cfg, SuView *root);
void su_ui_unmount(SuUiSession *session);

/* Sync helpers (also available as IO via delay thunks). */
int su_ui_pump_sync(SuUiSession *session);
int su_ui_inject_sync(SuUiSession *session, const SuInputEvent *event);
int su_ui_snapshot_png_sync(SuUiSession *session, const char *path);
int su_ui_snapshot_png_bytes(SuUiSession *session, uint8_t **out, size_t *out_len);

/* IO-shaped session ops (ADR 0004 / 0003). */
SuIo *su_ui_pump(SuUiSession *session);
SuIo *su_ui_inject(SuUiSession *session, SuInputEvent event);
SuIo *su_ui_snapshot_png(SuUiSession *session, const char *path);

SuUiRuntimeKind su_ui_session_kind(const SuUiSession *session);
int su_ui_session_width(const SuUiSession *session);
int su_ui_session_height(const SuUiSession *session);

/* Kernel-dialect demo: mount label → pump → optional snapshot → unmount.
 * SCALUI_SNAPSHOT_PATH / SCALUI_UI_GOLDEN env control output path.
 * Returns IO[Unit]. */
SuIo *su_ui_run_headless_label(const char *text, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_UI_H */
