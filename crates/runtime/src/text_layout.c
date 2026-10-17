#include "text.h"
#include "sk_capi.h"
#include <limits.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
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

static int group_after(const SzText *text, int index, int end, float font, float scale) {
  const SzTextRun *first = &text->runs[index];
  float size = run_font(first, font, scale);
  int next = index + 1;
  while (next < text->count && text->runs[next].start < end &&
         run_font(&text->runs[next], font, scale) == size &&
         (text->runs[next].style & 7) == (first->style & 7)) next++;
  return next;
}

static char *source_with_dots(const char *source, int length, int end) {
  if (length > INT_MAX - 3) sz_panic("Text is too large");
  char *result = sz_alloc((size_t)length + 4);
  memcpy(result, source, (size_t)end);
  memcpy(result + end, "...", 3);
  memcpy(result + end + 3, source + end, (size_t)(length - end) + 1);
  return result;
}

static float range_width(const SzText *text, int start, int end, float font, float scale, int dots) {
  float width = 0.f;
  if (!text) return width;
  const char *source = sz_text_source(text);
  char *joined = dots ? source_with_dots(source, text->length, end) : NULL;
  if (joined) source = joined;
  SkTextRange line = sk_text_slice(source, (size_t)text->length + (dots ? 3 : 0),
    (size_t)start, (size_t)end + (dots ? 3 : 0));
  for (int i = run_at(text, start); i < text->count && text->runs[i].start < end;) {
    int next = group_after(text, i, end, font, scale);
    const SzTextRun *run = &text->runs[i];
    int a = start > run->start ? start : run->start;
    int b = end < text->runs[next - 1].end ? end : text->runs[next - 1].end;
    width += sk_font_measure_span(sk_text_part(line, (size_t)a, (size_t)b + (dots && b == end ? 3 : 0)), run_font(run, font, scale), run->style);
    i = next;
  }
  if (dots && start == end) {
    int i = run_at(text, start);
    if (i == text->count && i > 0) i--;
    const SzTextRun *run = i < text->count ? &text->runs[i] : NULL;
    width = sk_font_measure_span(sk_text_slice(source, (size_t)text->length + 3, (size_t)end, (size_t)end + 3),
      run ? run_font(run, font, scale) : font, run ? run->style : 0);
  }
  sz_free(joined);
  return width;
}

typedef struct TextFragment {
  SkTextRange context;
  int start, end, style, colored, dots, action, order;
  uint32_t color;
  float x, baseline, width, font, height, context_x, key;
} TextFragment;

typedef struct TextFragments {
  TextFragment *values;
  int count, capacity, start;
  const float *leading, *trailing;
} TextFragments;

static void collect_fragment(SkTextRange context, int start, int end,
  float x, float baseline, float width, float font, float height,
  int style, uint32_t color, int colored, int dots, int action,
  float context_x, void *env) {
  TextFragments *fragments = env;
  if (fragments->count == fragments->capacity) {
    if (fragments->capacity > INT_MAX / 2) sz_panic("Text has too many fragments");
    int capacity = fragments->capacity ? fragments->capacity * 2 : 8;
    TextFragment *values = sz_alloc((size_t)capacity * sizeof(*values));
    if (fragments->count) memcpy(values, fragments->values, (size_t)fragments->count * sizeof(*values));
    sz_free(fragments->values); fragments->values = values; fragments->capacity = capacity;
  }
  float key = INFINITY;
  for (int byte = start; byte < end + (dots ? 3 : 0); byte++) {
    int at = byte - fragments->start;
    key = fminf(key, fminf(fragments->leading[at], fragments->trailing[at]));
  }
  int order = fragments->count++;
  fragments->values[order] = (TextFragment){context, start, end, style, colored, dots, action,
    order, color, x, baseline, width, font, height, context_x, key};
}

static int fragment_visual_order(const void *a, const void *b) {
  const TextFragment *first = a, *second = b;
  if (first->key < second->key) return -1;
  if (first->key > second->key) return 1;
  return (first->order > second->order) - (first->order < second->order);
}

static int fragment_source_order(const void *a, const void *b) {
  const TextFragment *first = a, *second = b;
  return (first->order > second->order) - (first->order < second->order);
}

static void emit_fragments(TextFragments *fragments, SzTextFragmentFn emit, void *env) {
  qsort(fragments->values, (size_t)fragments->count, sizeof(TextFragment), fragment_visual_order);
  float x = 0.f;
  for (int i = 0; i < fragments->count; i++) {
    TextFragment *part = &fragments->values[i];
    part->context_x += x - part->x;
    part->x = x;
    x += part->width;
  }
  /* Source order keeps browser selection and copying in the original text. */
  qsort(fragments->values, (size_t)fragments->count, sizeof(TextFragment), fragment_source_order);
  for (int i = 0; i < fragments->count; i++) {
    const TextFragment *part = &fragments->values[i];
    emit(part->context, part->start, part->end, part->x, part->baseline, part->width,
      part->font, part->height, part->style, part->color, part->colored, part->dots,
      part->action, part->context_x, env);
  }
  sz_free(fragments->values);
}

static void emit_part(SkTextRange context, int start, int end, float left, float right,
  float x, float baseline, float font, float height, const SzTextRun *run,
  SzTextFragmentFn emit, void *env) {
  emit(context, start, end, x + left, baseline, right - left, font, height,
    run->style, run->color, run->colored, 0, run->action, x, env);
}

static void emit_run(SkTextRange context, int start, int end, const float *leading,
  const float *trailing, float x, float baseline, float font, float height,
  const SzTextRun *run, const TextFragments *visual, SzTextFragmentFn emit, void *env) {
  const char *source = context.source;
  int first = start, ready = 0;
  float left = 0.f, right = 0.f, visual_left = 0.f, visual_right = 0.f;
  int direction = 0;
  for (int byte = start; byte < end;) {
    float a = fminf(leading[byte - context.start], trailing[byte - context.start]);
    float b = fmaxf(leading[byte - context.start], trailing[byte - context.start]);
    float va = visual ? visual->leading[byte - visual->start] : 0.f;
    float vb = visual ? visual->trailing[byte - visual->start] : 0.f;
    float vl = fminf(va, vb), vr = fmaxf(va, vb);
    int next_direction = vb < va ? -1 : vb > va ? 1 : direction;
    if (ready && (a > right + .01f || b < left - .01f ||
        (visual && (vl > visual_right + .01f || vr < visual_left - .01f || next_direction != direction)))) {
      emit_part(context, first, byte, left, right, x, baseline, font, height, run, emit, env);
      first = byte; ready = 0;
    }
    visual_left = ready ? fminf(visual_left, vl) : vl;
    visual_right = ready ? fmaxf(visual_right, vr) : vr;
    direction = next_direction;
    left = ready ? fminf(left, a) : a;
    right = ready ? fmaxf(right, b) : b;
    ready = 1;
    byte++;
    while (byte < end && ((unsigned char)source[byte] & 0xc0) == 0x80) byte++;
  }
  if (ready) emit_part(context, first, end, left, right, x, baseline, font, height, run, emit, env);
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
  /* Keep measured widths on one line after float rounding in parent layout. */
  float limit = width + 4.f * FLT_EPSILON * fmaxf(1.f, width);
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
    if (width <= 0.f || range_width(text, start, paragraph_end, font, scale, 0) <= limit) {
      i = end = next = paragraph_end;
    } else while (i < length && source[i] != '\n') {
      int after = next_cluster(breaks, i, length);
      if (source[i] == ' ' && breaks[i + 1]) space = i;
      if (width > 0.f && range_width(text, start, after, font, scale, 0) > limit) {
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
    float dots_width = sk_font_measure_span(sk_text_range("...", 3), dots_font, last->style);
    if (l == count - 1 && result.truncated && ellipsis) {
      for (;;) {
        style_index = run_at(text, line->end > line->start ? previous_byte(source, line->end, line->start) : line->start);
        last = text && style_index < text->count ? &text->runs[style_index] : &fallback;
        dots_font = run_font(last, font, scale);
        dots_width = sk_font_measure_span(sk_text_range("...", 3), dots_font, last->style);
        if (width <= 0.f || range_width(text, line->start, line->end, font, scale, 1) <= limit) {
          line->dots = 1;
          break;
        }
        if (line->end <= line->start) break;
        line->end = previous_cluster(breaks, line->end, line->start);
      }
    }
    line->width = range_width(text, line->start, line->end, font, scale, line->dots);
    char *joined = line->dots ? source_with_dots(source, length, line->end) : NULL;
    const char *line_source = joined ? joined : source;
    size_t line_bytes = (size_t)length + (line->dots ? 3 : 0);
    SkTextRange visible = sk_text_slice(line_source, line_bytes, (size_t)line->start,
      (size_t)line->end + (line->dots ? 3 : 0));
    float largest = line->dots ? dots_font : 0.f;
    float ascent = 0.f, descent = 0.f;
    if (line->dots) {
      sk_font_metrics(dots_font, last->style, &ascent, &descent);
      if (last->style & SK_TEXT_UNDERLINE)
        descent = fmaxf(descent, dots_font * .1f + fmaxf(1.f, dots_font / 16.f));
    }
    for (int i = run_at(text, line->start); text && line->end > line->start && i < text->count && text->runs[i].start < line->end;) {
      int next = group_after(text, i, line->end, font, scale);
      float size = run_font(&text->runs[i], font, scale);
      if (size > largest) largest = size;
      float a, d;
      int start = text->runs[i].start > line->start ? text->runs[i].start : line->start;
      int end = text->runs[next - 1].end;
      if (end > line->end) end = line->end;
      sk_font_span_metrics(sk_text_part(visible, (size_t)start, (size_t)end + (line->dots && end == line->end ? 3 : 0)), size, text->runs[i].style, &a, &d);
      float font_a, font_d;
      sk_font_metrics(size, text->runs[i].style, &font_a, &font_d);
      a = fmaxf(a, font_a); d = fmaxf(d, font_d);
      for (int j = i; j < next; j++) if (text->runs[j].style & SK_TEXT_UNDERLINE)
        d = fmaxf(d, size * .1f + fmaxf(1.f, size / 16.f));
      ascent = fmaxf(ascent, a);
      descent = fmaxf(descent, d);
      i = next;
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
    int first_group = run_at(text, line->start);
    int next_group = text && first_group < text->count ?
      group_after(text, first_group, line->end, font, scale) : first_group;
    int mixed = text && line->end > line->start && next_group < text->count &&
      text->runs[next_group].start < line->end;
    TextFragments fragments = {.start = line->start};
    float *visual_positions = NULL;
    SzTextFragmentFn output = emit;
    void *output_env = env;
    if (mixed && emit) {
      SkTextRange context = sk_text_slice(line_source, line_bytes, (size_t)line->start,
        (size_t)line->end + (line->dots ? 3 : 0));
      size_t count = context.end - context.start + 1;
      visual_positions = sz_alloc(count * 2 * sizeof(float));
      sk_font_text_positions(context, font, 0, visual_positions, visual_positions + count);
      fragments.leading = visual_positions; fragments.trailing = visual_positions + count;
      output = collect_fragment; output_env = &fragments;
    }
    float x = 0.f;
    for (int i = run_at(text, line->start); text && line->end > line->start && i < text->count && text->runs[i].start < line->end;) {
      int next = group_after(text, i, line->end, font, scale);
      int a = line->start > text->runs[i].start ? line->start : text->runs[i].start;
      int b = line->end < text->runs[next - 1].end ? line->end : text->runs[next - 1].end;
      float size = run_font(&text->runs[i], font, scale);
      SkTextRange context = sk_text_part(visible, (size_t)a,
        (size_t)b + (line->dots && b == line->end ? 3 : 0));
      float advance = sk_font_measure_span(context, size, text->runs[i].style);
      if (emit) {
        size_t positions = context.end - context.start + 1;
        float *leading = sz_alloc(positions * 2 * sizeof(float)), *trailing = leading + positions;
        sk_font_text_positions(context, size, text->runs[i].style, leading, trailing);
        for (int j = i; j < next; j++) {
          const SzTextRun *run = &text->runs[j];
          int start = a > run->start ? a : run->start;
          int end = b < run->end ? b : run->end;
          emit_run(context, start, end, leading, trailing, x, baseline, size, line_height,
            run, visual_positions ? &fragments : NULL, output, output_env);
        }
        if (line->dots && b == line->end) {
          float left = INFINITY, right = -INFINITY;
          for (int byte = b; byte < b + 3; byte++) {
            left = fminf(left, fminf(leading[byte - a], trailing[byte - a]));
            right = fmaxf(right, fmaxf(leading[byte - a], trailing[byte - a]));
          }
          output(context, b, b, x + left, baseline, right - left, dots_font, line_height,
            last->style, last->color, last->colored, 1, 0, x, output_env);
        }
        sz_free(leading);
      }
      x += advance;
      i = next;
    }
    if (emit && line->end == line->start && !line->dots) {
      SzTextRun empty = {0};
      const SzTextRun *run = text && inherited < text->count ? &text->runs[inherited] : &empty;
      emit(sk_text_slice(source, (size_t)length, (size_t)line->start, (size_t)line->end), line->start, line->end, 0.f, baseline, 0.f, line->font,
        line_height, run->style, run->color, run->colored, 0, run->action, 0.f, env);
    }
    if (line->dots && emit && line->end == line->start)
      emit(sk_text_slice(line_source, line_bytes, (size_t)line->end, (size_t)line->end + 3),
        line->end, line->end, 0.f, baseline, dots_width, dots_font, line_height,
        last->style, last->color, last->colored, 1, 0, 0.f, env);
    if (visual_positions) emit_fragments(&fragments, emit, env);
    sz_free(visual_positions);
    sz_free(joined);
    if (line->width > result.width) result.width = line->width;
    result.height += line_height;
  }
  result.lines = count;
  sz_free(breaks);
  sz_free(lines);
  return result;
}
