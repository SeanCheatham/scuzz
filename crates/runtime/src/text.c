#include "text.h"
#include "sk_capi.h"
#include <limits.h>
#include <string.h>

const char *sz_text_source(const SzText *text) {
  return text ? (const char *)(text->runs + text->count) : "";
}

static SzText *text_alloc(int count, int length) {
  if (count < 0 || length < 0 || (size_t)count >
      (SIZE_MAX - sizeof(SzText) - (size_t)length - 1) / sizeof(SzTextRun))
    sz_panic("Text is too large");
  size_t bytes = sizeof(SzText) + (size_t)count * sizeof(SzTextRun) + (size_t)length + 1;
  SzText *text = sz_rc_alloc(bytes, SZ_RC_RAW);
  memset(text, 0, bytes);
  text->count = count;
  text->length = length;
  return text;
}

SzText *sz_text_span(const SzString *value) {
  if (value && value->len > INT_MAX) sz_panic("Text is too large");
  int length = value ? (int)value->len : 0;
  SzText *text = text_alloc(1, length);
  if (length) {
    text->runs[0].end = length;
    memcpy((char *)sz_text_source(text), value->data, (size_t)length);
  }
  return text;
}

SzText *sz_text_concat(const SzList *values) {
  int count = 0, length = 0, actions = 0;
  for (const SzList *p = values; p; p = p->tail) {
    const SzText *text = p->head;
    if (!text) continue;
    if (text->count > INT_MAX - count || text->length > INT_MAX - length ||
        text->actions > INT_MAX - actions)
      sz_panic("Text is too large");
    count += text->count;
    length += text->length;
    actions += text->actions;
  }
  SzText *result = text_alloc(count, length);
  result->actions = actions;
  int at = 0, byte = 0, action = 0;
  for (const SzList *p = values; p; p = p->tail) {
    const SzText *text = p->head;
    if (!text) continue;
    for (int i = 0; i < text->count; i++) {
      SzTextRun run = text->runs[i];
      run.start += byte;
      run.end += byte;
      if (run.action) run.action += action;
      result->runs[at++] = run;
    }
    memcpy((char *)sz_text_source(result) + byte, sz_text_source(text), (size_t)text->length);
    byte += text->length;
    action += text->actions;
  }
  return result;
}

static SzText *text_copy(const SzText *source) {
  SzText *result = text_alloc(source ? source->count : 0, source ? source->length : 0);
  if (source) {
    result->actions = source->actions;
    memcpy(result->runs, source->runs, (size_t)source->count * sizeof(SzTextRun));
    memcpy((char *)sz_text_source(result), sz_text_source(source), (size_t)source->length);
  }
  return result;
}

SzText *sz_text_color(int64_t color, const SzText *source) {
  SzText *text = text_copy(source);
  for (int i = 0; i < text->count; i++) {
    text->runs[i].color = (uint32_t)color;
    text->runs[i].colored = 1;
  }
  return text;
}

SzText *sz_text_size(int64_t size, const SzText *source) {
  if (size < 1 || size > INT_MAX) sz_panic("Text size must be a positive 32-bit integer");
  SzText *text = text_copy(source);
  for (int i = 0; i < text->count; i++) text->runs[i].size = (int)size;
  return text;
}

static SzText *text_style(const SzText *source, int style) {
  SzText *text = text_copy(source);
  for (int i = 0; i < text->count; i++) text->runs[i].style |= style;
  return text;
}
SzText *sz_text_bold(const SzText *text) { return text_style(text, SK_TEXT_BOLD); }
SzText *sz_text_italic(const SzText *text) { return text_style(text, SK_TEXT_ITALIC); }
SzText *sz_text_underline(const SzText *text) { return text_style(text, SK_TEXT_UNDERLINE); }
SzText *sz_text_monospace(const SzText *text) { return text_style(text, SK_TEXT_MONO); }
SzString *sz_text_plain(const SzText *text) {
  return sz_string_from_bytes(sz_text_source(text), text ? text->length : 0);
}


SzText *sz_text_action(int64_t key, const SzText *source) {
  SzText *text = text_copy(source);
  text->actions = text->count ? 1 : 0;
  for (int i = 0; i < text->count; i++) {
    text->runs[i].style |= SK_TEXT_UNDERLINE;
    text->runs[i].action = 1;
    text->runs[i].key = key;
  }
  return text;
}
