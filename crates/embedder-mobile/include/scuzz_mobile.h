#ifndef SCUZZ_MOBILE_H
#define SCUZZ_MOBILE_H

#include "scuzz_ui.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mobile embedder ABI. Weak stubs live in runtime when unlinked. */

/* Nonzero if a mobile shell can present (host: SCUZZ_MOBILE_SHELL=1). */
int sz_mobile_available(void);

/* Present an RGBA8888 frame. Returns 1 on success, 0 if unavailable / failed.
 * point_w/point_h are the window size in points. pixel_w/pixel_h are the
 * rgba dimensions (point * backing scale). */
int sz_mobile_present(const char *title, int point_w, int point_h, int pixel_w,
                      int pixel_h, const uint8_t *rgba, size_t nbytes);

/* Destroy shell resources. */
void sz_mobile_shutdown(void);

/* Soft keyboard show/hide request from the session (TextField focus). */
void sz_mobile_set_keyboard(int visible);

/* Pop one queued OS event into out. Returns 1 if an event was written. */
int sz_mobile_poll_event(SzInputEvent *out);

/* Nonzero while the shell keeps the app live. Ui.run pumps until this is 0.
 * Host shell returns 0 so the CI smoke stays a single frame. */
int sz_mobile_alive(void);

/* Enqueue an OS event. Returns 1 if stored. */
int sz_mobile_push_event(const SzInputEvent *event);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_MOBILE_H */
