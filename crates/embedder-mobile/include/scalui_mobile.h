#ifndef SCALUI_MOBILE_H
#define SCALUI_MOBILE_H

#include "scalui_ui.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mobile embedder ABI (Phase 5). Weak stubs live in runtime when unlinked.
 *
 * Host shell (Linux CI): present + scripted event queue when SCALUI_MOBILE_SHELL=1.
 * Android / iOS packaging shells map OS touch / lifecycle / IME onto SuInputEvent
 * and call the same present / keyboard hooks.
 */

/* Nonzero if a mobile shell can present (host: SCALUI_MOBILE_SHELL=1). */
int su_mobile_available(void);

/* Present an RGBA8888 frame. Returns 1 on success, 0 if unavailable / failed. */
int su_mobile_present(const char *title, int width, int height,
                      const uint8_t *rgba, size_t nbytes);

/* Destroy shell resources. */
void su_mobile_shutdown(void);

/* Soft keyboard show/hide request from the session (TextField focus). */
void su_mobile_set_keyboard(int visible);

/* Pop one queued OS event into out. Returns 1 if an event was written. */
int su_mobile_poll_event(SuInputEvent *out);

/* Host / test helper: push an event into the shell queue. */
int su_mobile_push_event(const SuInputEvent *event);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_MOBILE_H */
