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

/* Shared IO combinators: build the graph node, then drop the input refs. */
static inline SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}

static inline SzIo *pure_drop(void *value) {
  SzIo *io = sz_io_pure(value);
  sz_release(value);
  return io;
}

static inline SzIo *fail_drop(SzError *err) {
  SzIo *io = sz_io_fail(err);
  sz_release(err);
  return io;
}

static inline void *rc_box_zero(size_t n) {
  void *p = sz_rc_alloc(n, SZ_RC_BOX);
  memset(p, 0, n);
  return p;
}

static inline SzIo *race_drop(SzIo *left, SzIo *right) {
  SzIo *io = sz_io_race(left, right);
  sz_release(left);
  sz_release(right);
  return io;
}

static inline SzString *pack_path(void *env) {
  SzPair *pack = (SzPair *)env;
  return pack ? (SzString *)pack->left : NULL;
}

#endif
