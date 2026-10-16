#ifndef SCUZZ_SK_TEXT_H
#define SCUZZ_SK_TEXT_H

#include <stddef.h>

/* bytes is the complete source length. The selected font range uses start and end.
 * line_start and line_end bound the visible shaping context. Ends are exclusive. */
typedef struct SkTextRange {
  const char *source;
  size_t bytes, start, end, line_start, line_end;
} SkTextRange;

static inline SkTextRange sk_text_slice(const char *source, size_t bytes, size_t start, size_t end) {
  SkTextRange range = {source, bytes, start, end, start, end};
  return range;
}

static inline SkTextRange sk_text_range(const char *source, size_t bytes) {
  return sk_text_slice(source, bytes, 0, bytes);
}

static inline SkTextRange sk_text_part(SkTextRange line, size_t start, size_t end) {
  line.start = start; line.end = end;
  return line;
}

static inline int sk_text_valid(SkTextRange range) {
  return range.line_start <= range.start && range.start <= range.end &&
    range.end <= range.line_end && range.line_end <= range.bytes;
}

#endif
