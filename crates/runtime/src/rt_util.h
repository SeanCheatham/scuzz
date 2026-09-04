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

/* Grow a C string buffer. Used by signal dump, a11y dump, and inject read. */
static inline void sz_dump_append(char **buf, size_t *len, size_t *cap,
                                  const char *s) {
  size_t n = strlen(s);
  if (*len + n + 1 > *cap) {
    size_t ncap = *cap ? *cap : 256;
    char *nb;
    while (*len + n + 1 > ncap)
      ncap *= 2;
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

/* Editor dump dialect: \\ \" \n \r \t. No surrounding quotes. */
static inline void sz_dump_append_escaped(char **buf, size_t *len, size_t *cap,
                                          const char *s) {
  const char *p;
  if (!s)
    return;
  for (p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\\')
      sz_dump_append(buf, len, cap, "\\\\");
    else if (c == '"')
      sz_dump_append(buf, len, cap, "\\\"");
    else if (c == '\n')
      sz_dump_append(buf, len, cap, "\\n");
    else if (c == '\r')
      sz_dump_append(buf, len, cap, "\\r");
    else if (c == '\t')
      sz_dump_append(buf, len, cap, "\\t");
    else {
      char t[2];
      t[0] = (char)c;
      t[1] = '\0';
      sz_dump_append(buf, len, cap, t);
    }
  }
}

static inline unsigned char sz_dump_unescape_char(const char **p) {
  const char *s = *p;
  unsigned char c;
  if (*s == '\\' && s[1]) {
    s++;
    if (*s == 'n')
      c = '\n';
    else if (*s == 'r')
      c = '\r';
    else if (*s == 't')
      c = '\t';
    else
      c = (unsigned char)*s;
    s++;
    *p = s;
    return c;
  }
  c = (unsigned char)*s;
  *p = s + 1;
  return c;
}

/* Unescape a dump/script payload. Caller frees. */
static inline char *sz_dump_unescape(const char *s) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  const char *p = s ? s : "";
  while (*p) {
    char t[2];
    t[0] = (char)sz_dump_unescape_char(&p);
    t[1] = '\0';
    sz_dump_append(&buf, &len, &cap, t);
  }
  if (!buf)
    sz_dump_append(&buf, &len, &cap, "");
  return buf;
}

/* Parse `"…"` with the dump escape dialect. p points at the opening quote.
 * Returns the pointer after the closing quote, or NULL. When out is set,
 * writes the unescaped bytes (caller frees). */
static inline const char *sz_dump_parse_quoted(const char *p, char **out) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  if (out)
    *out = NULL;
  if (!p || *p != '"')
    return NULL;
  p++;
  while (*p && *p != '"') {
    char t[2];
    t[0] = (char)sz_dump_unescape_char(&p);
    t[1] = '\0';
    if (out)
      sz_dump_append(&buf, &len, &cap, t);
  }
  if (*p != '"') {
    sz_free(buf);
    return NULL;
  }
  if (out) {
    if (!buf)
      sz_dump_append(&buf, &len, &cap, "");
    *out = buf;
  }
  return p + 1;
}

#endif
