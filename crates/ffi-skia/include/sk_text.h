#ifndef SCUZZ_SK_TEXT_H
#define SCUZZ_SK_TEXT_H

#include <stddef.h>

/* Keep paragraph context for the selected UTF-8 byte range.
 * bytes is the complete source length. Do not include the byte at end. */
typedef struct SkTextRange {
  const char *source;
  size_t bytes, start, end;
} SkTextRange;

static inline SkTextRange sk_text_range(const char *source, size_t bytes) {
  SkTextRange range = {source, bytes, 0, bytes};
  return range;
}

#endif
