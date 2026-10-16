#ifndef SCUZZ_WEB_H
#define SCUZZ_WEB_H
#include "scuzz_ui.h"
#include "sk_text.h"
void sz_web_stop(void);
void sz_web_set_title(const char *title);
void sz_web_start(SzUiSession *session);
/* Start the live rAF loop. Return so a later JS ccall is a new WASM entry.
 * An idle frame pauses the loop. Keep the loop mounted so the runtime stays
 * live. */
void sz_web_live_loop(void (*frame)(void));
/* Pause the rAF loop. Call from the frame callback when the session has no
 * pending work. */
void sz_web_loop_pause(void);
/* Resume a paused rAF loop. The runtime calls this when new work marks the
 * session dirty. */
void sz_web_idle_wake(void);
void sz_web_present(int width, int height, const uint8_t *rgba);
void sz_web_frame_begin(float scale);
void sz_web_frame_end(void);
void sz_web_text_begin(SzView *view, const char *text, int level, uint32_t ink, uint32_t selection);
void sz_web_text_line(const char *source, SkTextRange context, float context_x, int start, int end, const char *text,
                      float x, float y, float width, float font, float line_h,
                      int clipped, float cx, float cy, float cw, float ch, uint32_t ink, int style, int synthetic, uintptr_t action);
void sz_web_transition(SzView *view, int value, int target, int remaining, int suppressed);
void sz_web_select_state(SzView *view, SzView *popup, SzView *active, int count);
void sz_web_select_list(SzView *view, SzView *popup, SzRect frame);
void sz_web_select_option(SzView *option, SzView *view, int index, int count);

void sz_web_tooltip(SzView *view, const char *text, SzRect frame, int open);
void sz_web_inline_action(SzView *text, const SzRect *boxes, int count);
int sz_web_book_begin(void);
void sz_web_section(const char *id, const char *title, int selected);
void sz_web_group_begin(SzView *view, int role, const char *label, SzView *related, int hidden, float x, float y, float w, float h);
void sz_web_group_end(void);
void sz_web_control(SzView *view, int role, const char *label, const char *route,
  const char *copy, const char *value, SzView *related, int checked, float x, float y, float w, float h,
  int clipped, float cx, float cy, float cw, float ch, uint32_t ink, uint32_t selection,
  float font, float line_h, float inset, float gutter);
void sz_web_copy(SzView *view, const char *text);
SzView *sz_view_web_find(SzView *root, uintptr_t id);
int sz_view_web_navigate(SzView *root, const char *title);
#endif
