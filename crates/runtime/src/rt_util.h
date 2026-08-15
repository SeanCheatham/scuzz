#ifndef SCUZZ_RT_UTIL_H
#define SCUZZ_RT_UTIL_H

#include "scuzz_rt.h"

#include <string.h>

static inline char *sz_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)sz_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

#endif
