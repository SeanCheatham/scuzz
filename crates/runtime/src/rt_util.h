#ifndef SCUZZ_RT_UTIL_H
#define SCUZZ_RT_UTIL_H

#include "scuzz_rt.h"

#include <stdio.h>
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

/* Grow a C string buffer. Used by signal dump, a11y dump, and inject read. */
static inline void sz_dump_append(char **buf, size_t *len, size_t *cap,
                                  const char *s) {
  size_t n = strlen(s);
  if (n > SIZE_MAX - *len - 1)
    sz_panic("sz_dump_append: buffer size overflow");
  if (*len + n + 1 > *cap) {
    size_t ncap = *cap ? *cap : 256;
    char *nb;
    while (*len + n + 1 > ncap) {
      if (ncap > SIZE_MAX / 2)
        sz_panic("sz_dump_append: buffer size overflow");
      ncap *= 2;
    }
    nb = (char *)sz_alloc(ncap);
    if (*buf) {
      memcpy(nb, *buf, *len);
      sz_free(*buf);
    }
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}

/* JSON string body escaping for dumps: \\ \" \n \r \t, \u00xx below 0x20. */
static inline void sz_json_fputs_escaped(FILE *f, const char *s) {
  const char *p;
  if (!s)
    return;
  for (p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\\')
      fputs("\\\\", f);
    else if (c == '"')
      fputs("\\\"", f);
    else if (c == '\n')
      fputs("\\n", f);
    else if (c == '\r')
      fputs("\\r", f);
    else if (c == '\t')
      fputs("\\t", f);
    else if (c < 0x20)
      fprintf(f, "\\u%04x", c);
    else
      fputc(*p, f);
  }
}

#endif
