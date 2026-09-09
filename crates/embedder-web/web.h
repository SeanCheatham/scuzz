#ifndef SCUZZ_WEB_H
#define SCUZZ_WEB_H
#include "scuzz_ui.h"
void sz_web_stop(void);
void sz_web_start(SzUiSession *session);
void sz_web_present(int width, int height, const uint8_t *rgba);
void sz_web_frame_begin(float scale);
void sz_web_frame_end(void);
void sz_web_text_begin(const char *text);
void sz_web_text_line(const char *source, int start, int end, const char *text,
                      float x, float y, float width, float font, float line_h,
                      int clipped, float cx, float cy, float cw, float ch);
int sz_web_book_begin(void);
void sz_web_section(const char *title, int selected);
int sz_view_web_navigate(SzView *root, const char *title);
#endif
