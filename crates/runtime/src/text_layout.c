#include "text.h"
#include "sk_capi.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static int next_cluster(const uint8_t *breaks, int byte, int end) {
  byte++;
  while (byte < end && !breaks[byte]) byte++;
  return byte;
}
static int previous_cluster(const uint8_t *breaks, int byte, int start) {
  byte--;
  while (byte > start && !breaks[byte]) byte--;
  return byte;
}
static int previous_byte(const char *source, int byte, int start) {
  byte--;
  while (byte > start && ((unsigned char)source[byte] & 0xc0) == 0x80) byte--;
  return byte;
}
static int run_at(const SzText *text, int byte) {
  int lo = 0, hi = text ? text->count : 0;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (text->runs[mid].end <= byte) lo = mid + 1; else hi = mid;
  }
  return lo;
}
static float run_font(const SzTextRun *run, float font, float scale) {
  return run->size ? (float)run->size * scale : font;
}

static float range_width(const SzText *text, int start, int end, float font, float scale) {
  float width = 0.f;
  if (!text || start >= end) return width;
  const char *source = sz_text_source(text);
  for (int i = run_at(text, start); i < text->count && text->runs[i].start < end; i++) {
    const SzTextRun *run = &text->runs[i];
    int a = start > run->start ? start : run->start;
    int b = end < run->end ? end : run->end;
    width += sk_font_measure_span(source + a, (size_t)(b - a), run_font(run, font, scale), run->style);
  }
  return width;
}

typedef struct TextLine { int start, end, dots; float width, font; } TextLine;

static void add_line(TextLine **lines, int *count, int *capacity, int start, int end) {
  if (*count == *capacity) {
    if (*capacity > INT_MAX / 2) sz_panic("Text has too many lines");
    int next = *capacity ? *capacity * 2 : 8;
    TextLine *grown = sz_alloc((size_t)next * sizeof(TextLine));
    if (*count) memcpy(grown, *lines, (size_t)*count * sizeof(TextLine));
    sz_free(*lines);
    *lines = grown;
    *capacity = next;
  }
  (*lines)[(*count)++] = (TextLine){.start = start, .end = end};
}

SzTextMetrics sz_text_layout(const SzText *text, float font, float scale, float gap,
  float width, int max_lines, int ellipsis, SzTextFragmentFn emit, void *env) {
  SzTextMetrics result = {0};
  const char *source = sz_text_source(text);
  int length = text ? text->length : 0;
  uint8_t *breaks = sz_alloc((size_t)length + 1);
  sk_text_cluster_breaks(source, (size_t)length, breaks);
  TextLine *lines = NULL;
  int count = 0, capacity = 0, start = 0;
  for (;;) {
    int i = start, space = -1, end = start, next = start;
    int paragraph_end = start;
    while (paragraph_end < length && source[paragraph_end] != '\n') paragraph_end++;
    if (width <= 0.f || range_width(text, start, paragraph_end, font, scale) <= width) {
      i = end = next = paragraph_end;
    } else while (i < length && source[i] != '\n') {
      int after = next_cluster(breaks, i, length);
      if (source[i] == ' ' && breaks[i + 1]) space = i;
      if (width > 0.f && range_width(text, start, after, font, scale) > width) {
        if (space >= start) {
          end = space;
          next = space + 1;
          while (next < length && source[next] == ' ' && breaks[next + 1]) next++;
        } else { end = i > start ? i : after; next = end; }
        break;
      }
      i = after;
      end = next = i;
    }
    int newline = i < length && source[i] == '\n';
    if (newline) { end = i; next = i + 1; }
    while (end > start && source[end - 1] == ' ') end--;
    add_line(&lines, &count, &capacity, start, end);
    if (max_lines > 0 && count > max_lines) { result.truncated = 1; count = max_lines; break; }
    if (next >= length && !newline) break;
    start = next;
  }
  for (int l = 0; l < count; l++) {
    TextLine *line = &lines[l];
    int style_index = run_at(text, line->end > line->start ? previous_byte(source, line->end, line->start) : line->start);
    SzTextRun fallback = {0};
    const SzTextRun *last = text && style_index < text->count ? &text->runs[style_index] : &fallback;
    float dots_font = run_font(last, font, scale);
    float dots_width = sk_font_measure_span("...", 3, dots_font, last->style);
    if (l == count - 1 && result.truncated && ellipsis) {
      for (;;) {
        style_index = run_at(text, line->end > line->start ? previous_byte(source, line->end, line->start) : line->start);
        last = text && style_index < text->count ? &text->runs[style_index] : &fallback;
        dots_font = run_font(last, font, scale);
        dots_width = sk_font_measure_span("...", 3, dots_font, last->style);
        if (width <= 0.f || range_width(text, line->start, line->end, font, scale) + dots_width <= width) {
          line->dots = 1;
          break;
        }
        if (line->end <= line->start) break;
        line->end = previous_cluster(breaks, line->end, line->start);
      }
    }
    line->width = range_width(text, line->start, line->end, font, scale) + (line->dots ? dots_width : 0.f);
    float largest = line->dots ? dots_font : 0.f;
    float ascent = 0.f, descent = 0.f;
    if (line->dots) {
      sk_font_metrics(dots_font, last->style, &ascent, &descent);
      if (last->style & SK_TEXT_UNDERLINE)
        descent = fmaxf(descent, dots_font * .1f + fmaxf(1.f, dots_font / 16.f));
    }
    for (int i = run_at(text, line->start); text && line->end > line->start && i < text->count && text->runs[i].start < line->end; i++) {
      float size = run_font(&text->runs[i], font, scale);
      if (size > largest) largest = size;
      float a, d;
      int start = text->runs[i].start > line->start ? text->runs[i].start : line->start;
      int end = text->runs[i].end;
      if (end > line->end) end = line->end;
      sk_font_span_metrics(source + start, (size_t)(end - start), size, text->runs[i].style, &a, &d);
      float font_a, font_d;
      sk_font_metrics(size, text->runs[i].style, &font_a, &font_d);
      a = fmaxf(a, font_a); d = fmaxf(d, font_d);
      if (text->runs[i].style & SK_TEXT_UNDERLINE)
        d = fmaxf(d, size * .1f + fmaxf(1.f, size / 16.f));
      ascent = fmaxf(ascent, a);
      descent = fmaxf(descent, d);
    }
    int inherited = run_at(text, line->start);
    if (text && inherited == text->count && inherited > 0) inherited--;
    float empty_font = text && inherited < text->count ? run_font(&text->runs[inherited], font, scale) : font;
    line->font = largest > 0.f ? largest : empty_font;
    if (largest <= 0.f) {
      int style = text && inherited < text->count ? text->runs[inherited].style : 0;
      sk_font_metrics(empty_font, style, &ascent, &descent);
    }
    float base = fmaxf(line->font, ascent) + 2.f * scale;
    float line_height = fmaxf(line->font + gap, base + descent);
    float baseline = result.height + base;
    float x = 0.f;
    for (int i = run_at(text, line->start); text && line->end > line->start && i < text->count && text->runs[i].start < line->end; i++) {
      const SzTextRun *run = &text->runs[i];
      int a = line->start > run->start ? line->start : run->start;
      int b = line->end < run->end ? line->end : run->end;
      float size = run_font(run, font, scale);
      float advance = sk_font_measure_span(source + a, (size_t)(b - a), size, run->style);
      if (emit) emit(source, a, b, x, baseline, advance, size, line_height,
                     run->style, run->color, run->colored, 0, run->action, env);
      x += advance;
    }
    if (emit && line->end == line->start && !line->dots) {
      SzTextRun empty = {0};
      const SzTextRun *run = text && inherited < text->count ? &text->runs[inherited] : &empty;
      emit(source, line->start, line->end, 0.f, baseline, 0.f, line->font,
        line_height, run->style, run->color, run->colored, 0, run->action, env);
    }
    if (line->dots && emit) emit(source, line->end, line->end, x, baseline,
      dots_width, dots_font, line_height, last->style, last->color, last->colored, 1, 0, env);
    if (line->width > result.width) result.width = line->width;
    result.height += line_height;
  }
  result.lines = count;
  sz_free(breaks);
  sz_free(lines);
  return result;
}
