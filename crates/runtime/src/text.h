#ifndef SCUZZ_TEXT_H
#define SCUZZ_TEXT_H

#include "scuzz_ui.h"

/* Offsets use UTF-8 bytes in the complete source text. */
typedef struct SzTextRun {
  int start, end;
  int size;
  uint32_t color;
  int colored;
  int style;
  int action;
  int64_t key;
} SzTextRun;

struct SzText {
  int length, count, actions;
  SzTextRun runs[];
};

const char *sz_text_source(const SzText *text);
typedef void (*SzTextFragmentFn)(const char *source, int start, int end,
  float x, float baseline, float width, float font, float line_height,
  int style, uint32_t color, int colored, int ellipsis, int action, void *env);
typedef struct SzTextMetrics { float width, height; int lines, truncated; } SzTextMetrics;
SzTextMetrics sz_text_layout(const SzText *text, float font, float scale, float gap,
  float width, int max_lines, int ellipsis, SzTextFragmentFn emit, void *env);

#endif
