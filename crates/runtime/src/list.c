#include "scuzz_rt.h"

#include <string.h>

/* Linked list: NULL = Nil. Cons cells retain head and tail. sz_list_free
 * is sz_release on the root cell. */

SzList *sz_list_nil(void) { return NULL; }

int sz_list_is_empty(const SzList *xs) { return xs == NULL; }

SzList *sz_list_cons(void *head, SzList *tail) {
  SzList *n = (SzList *)sz_rc_alloc(sizeof(SzList), SZ_RC_LIST);
  n->head = head;
  n->tail = tail;
  sz_retain(head);
  sz_retain(tail);
  return n;
}

/* Cons, then drop the caller's unique ref to `tail`. Use when `tail` is a
 * fresh spine the caller will not keep. Sharing a live tail uses cons only. */
static SzList *sz_list_cons_take(void *head, SzList *tail) {
  SzList *n = sz_list_cons(head, tail);
  sz_release(tail);
  return n;
}

void *sz_list_head(const SzList *xs) {
  if (!xs)
    sz_panic("List.head on empty");
  return xs->head;
}

SzList *sz_list_tail(const SzList *xs) {
  if (!xs)
    sz_panic("List.tail on empty");
  return xs->tail;
}

size_t sz_list_len(const SzList *xs) {
  size_t n = 0;
  for (const SzList *p = xs; p; p = p->tail)
    n++;
  return n;
}

void *sz_list_at(const SzList *xs, size_t index) {
  const SzList *p = xs;
  size_t i = 0;
  while (p) {
    if (i == index)
      return p->head;
    p = p->tail;
    i++;
  }
  sz_panic("List.at out of bounds");
  return NULL;
}

SzList *sz_list_reverse(SzList *xs) {
  SzList *acc = NULL;
  for (SzList *p = xs; p; p = p->tail)
    acc = sz_list_cons_take(p->head, acc);
  return acc;
}

/* Copy the spine and retain `x` (same as cons). The caller drops an owned
 * `x` after the call. */
SzList *sz_list_append(SzList *xs, void *x) {
  if (!xs)
    return sz_list_cons(x, NULL);
  return sz_list_cons_take(xs->head, sz_list_append(xs->tail, x));
}

static SzList *sz_list_copy(SzList *xs) {
  if (!xs)
    return NULL;
  return sz_list_cons_take(xs->head, sz_list_copy(xs->tail));
}

/* Replace the head at `index`. Copy the spine so `xs` is not mutated.
 * Out of range (empty, negative, or past the end) returns `xs` with an extra
 * retain so the caller can drop their ref. */
SzList *sz_list_set_at(SzList *xs, int64_t index, void *v) {
  SzList *rest;
  if (!xs || index < 0) {
    sz_retain(xs);
    return xs;
  }
  if (index == 0)
    return sz_list_cons_take(v, sz_list_copy(xs->tail));
  rest = sz_list_set_at(xs->tail, index - 1, v);
  if (rest == xs->tail) {
    sz_release(rest);
    sz_retain(xs);
    return xs;
  }
  return sz_list_cons_take(xs->head, rest);
}

SzList *sz_list_filter(SzList *xs, SzListPred pred, void *env) {
  if (!pred)
    sz_panic("sz_list_filter(null pred)");
  if (!xs)
    return NULL;
  if (pred(xs->head, env))
    return sz_list_cons_take(xs->head, sz_list_filter(xs->tail, pred, env));
  return sz_list_filter(xs->tail, pred, env);
}

SzList *sz_list_filter_not(SzList *xs, SzListPred pred, void *env) {
  if (!pred)
    sz_panic("sz_list_filter_not(null pred)");
  if (!xs)
    return NULL;
  if (!pred(xs->head, env))
    return sz_list_cons_take(xs->head, sz_list_filter_not(xs->tail, pred, env));
  return sz_list_filter_not(xs->tail, pred, env);
}

int64_t sz_list_count(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  int64_t n = 0;
  if (!pred)
    sz_panic("sz_list_count(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env))
      n++;
  }
  return n;
}

SzList *sz_list_take(SzList *xs, int64_t n) {
  if (!xs || n <= 0)
    return NULL;
  return sz_list_cons_take(xs->head, sz_list_take(xs->tail, n - 1));
}

SzList *sz_list_drop(SzList *xs, int64_t n) {
  SzList *p = xs;
  int64_t i = 0;
  if (n < 0)
    n = 0;
  while (p && i < n) {
    p = p->tail;
    i++;
  }
  sz_retain(p);
  return p;
}

SzList *sz_list_take_right(SzList *xs, int64_t n) {
  size_t len;
  if (!xs || n <= 0)
    return NULL;
  len = sz_list_len(xs);
  if ((uint64_t)n >= (uint64_t)len)
    return sz_list_drop(xs, 0);
  return sz_list_drop(xs, (int64_t)(len - (size_t)n));
}

SzList *sz_list_drop_right(SzList *xs, int64_t n) {
  size_t len;
  if (!xs)
    return NULL;
  if (n <= 0)
    return sz_list_drop(xs, 0);
  len = sz_list_len(xs);
  if ((uint64_t)n >= (uint64_t)len)
    return NULL;
  return sz_list_take(xs, (int64_t)(len - (size_t)n));
}

SzList *sz_list_init(SzList *xs) {
  if (!xs || !xs->tail)
    return NULL;
  return sz_list_cons_take(xs->head, sz_list_init(xs->tail));
}

SzList *sz_list_last(SzList *xs) {
  SzList *p = xs;
  if (!p)
    return NULL;
  while (p->tail)
    p = p->tail;
  return sz_list_cons(p->head, NULL);
}

/* Borrowed head at `index`, or `dflt`. Negative / OOB is `dflt`. */
void *sz_list_get_or(SzList *xs, int64_t index, void *dflt) {
  SzList *p = xs;
  int64_t i = 0;
  if (index < 0)
    return dflt;
  while (p) {
    if (i == index)
      return p->head;
    p = p->tail;
    i++;
  }
  return dflt;
}

/* `n` copies of `x`. n <= 0 is empty. Cons retains `x`. */
SzList *sz_list_fill(int64_t n, void *x) {
  SzList *acc = NULL;
  int64_t i;
  if (n <= 0)
    return NULL;
  for (i = 0; i < n; i++)
    acc = sz_list_cons_take(x, acc);
  return acc;
}

SzList *sz_list_find(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  if (!pred)
    sz_panic("sz_list_find(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env))
      return sz_list_cons(p->head, NULL);
  }
  return NULL;
}

int64_t sz_list_exists(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  if (!pred)
    sz_panic("sz_list_exists(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env))
      return 1;
  }
  return 0;
}

SzList *sz_list_takewhile(SzList *xs, SzListPred pred, void *env) {
  if (!pred)
    sz_panic("sz_list_takewhile(null pred)");
  if (!xs || !pred(xs->head, env))
    return NULL;
  return sz_list_cons_take(xs->head, sz_list_takewhile(xs->tail, pred, env));
}

SzList *sz_list_dropwhile(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  if (!pred)
    sz_panic("sz_list_dropwhile(null pred)");
  for (p = xs; p && pred(p->head, env); p = p->tail)
    ;
  sz_retain(p);
  return p;
}

int64_t sz_list_forall(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  if (!pred)
    sz_panic("sz_list_forall(null pred)");
  for (p = xs; p; p = p->tail) {
    if (!pred(p->head, env))
      return 0;
  }
  return 1;
}

SzList *sz_list_concat(SzList *xs, SzList *ys) {
  if (!xs) {
    sz_retain(ys);
    return ys;
  }
  return sz_list_cons_take(xs->head, sz_list_concat(xs->tail, ys));
}

SzList *sz_list_flatten(SzList *xss) {
  SzList *rest;
  SzList *out;
  if (!xss)
    return NULL;
  rest = sz_list_flatten(xss->tail);
  out = sz_list_concat((SzList *)xss->head, rest);
  sz_release(rest);
  return out;
}

SzList *sz_list_map(SzList *xs, SzListMapFn fn, void *env) {
  void *h;
  SzList *n;
  if (!fn)
    sz_panic("sz_list_map(null fn)");
  if (!xs)
    return NULL;
  /* Mapper returns +1. Cons retains. Drop the mapper ref. */
  h = fn(xs->head, env);
  n = sz_list_cons_take(h, sz_list_map(xs->tail, fn, env));
  sz_release(h);
  return n;
}

SzList *sz_list_flat_map(SzList *xs, SzListMapFn fn, void *env) {
  SzList *chunk;
  SzList *rest;
  SzList *out;
  if (!fn)
    sz_panic("sz_list_flat_map(null fn)");
  if (!xs)
    return NULL;
  /* Mapper returns +1 list. Concat retains. Drop the mapper list. */
  chunk = (SzList *)fn(xs->head, env);
  rest = sz_list_flat_map(xs->tail, fn, env);
  out = sz_list_concat(chunk, rest);
  sz_release(chunk);
  sz_release(rest);
  return out;
}

SzList *sz_list_pad_to(SzList *xs, int64_t n, void *x) {
  size_t len = sz_list_len(xs);
  SzList *suffix;
  SzList *out;
  if (n < 0)
    n = 0;
  if ((uint64_t)n <= (uint64_t)len)
    return sz_list_drop(xs, 0);
  suffix = sz_list_fill(n - (int64_t)len, x);
  out = sz_list_concat(xs, suffix);
  sz_release(suffix);
  return out;
}

SzList *sz_list_range(int64_t from, int64_t until) {
  SzList *acc = NULL;
  int64_t i;
  void *b;
  if (until <= from)
    return NULL;
  i = until;
  while (i > from) {
    i--;
    b = sz_box_i64(i);
    acc = sz_list_cons_take(b, acc);
    sz_release(b);
  }
  return acc;
}

SzList *sz_list_tabulate(int64_t n, SzListMapFn fn, void *env) {
  SzList *acc;
  int64_t i;
  void *idx;
  void *h;
  if (!fn)
    sz_panic("sz_list_tabulate(null fn)");
  if (n <= 0)
    return NULL;
  acc = NULL;
  for (i = n - 1; i >= 0; i--) {
    idx = sz_box_i64(i);
    h = fn(idx, env);
    sz_release(idx);
    acc = sz_list_cons_take(h, acc);
    sz_release(h);
  }
  return acc;
}

SzList *sz_list_intersperse(SzList *xs, void *x) {
  SzList *rest;
  SzList *mid;
  if (!xs)
    return NULL;
  if (!xs->tail) {
    sz_retain(xs);
    return xs;
  }
  rest = sz_list_intersperse(xs->tail, x);
  mid = sz_list_cons_take(x, rest);
  return sz_list_cons_take(xs->head, mid);
}

SzList *sz_list_grouped(SzList *xs, int64_t n) {
  SzList *chunk;
  SzList *rest;
  SzList *more;
  SzList *out;
  if (!xs || n <= 0)
    return NULL;
  chunk = sz_list_take(xs, n);
  rest = sz_list_drop(xs, n);
  more = sz_list_grouped(rest, n);
  sz_release(rest);
  out = sz_list_cons_take(chunk, more);
  sz_release(chunk);
  return out;
}

SzList *sz_list_sliding(SzList *xs, int64_t n) {
  SzList *window;
  SzList *more;
  SzList *out;
  if (!xs || n <= 0)
    return NULL;
  if ((uint64_t)n > (uint64_t)sz_list_len(xs))
    return NULL;
  window = sz_list_take(xs, n);
  more = sz_list_sliding(xs->tail, n);
  out = sz_list_cons_take(window, more);
  sz_release(window);
  return out;
}

int sz_list_non_empty(const SzList *xs) { return xs != NULL; }

void sz_list_free(SzList *xs) { sz_release(xs); }

SzString *sz_list_join(const SzList *xs, const char *sep) {
  if (!sep)
    sep = "";
  size_t sep_len = strlen(sep);
  size_t total = 0;
  size_t count = 0;
  for (const SzList *p = xs; p; p = p->tail) {
    SzString *s = (SzString *)p->head;
    if (s)
      total += s->len;
    if (count > 0)
      total += sep_len;
    count++;
  }
  char *buf = (char *)sz_alloc(total + 1);
  size_t off = 0;
  size_t i = 0;
  for (const SzList *p = xs; p; p = p->tail) {
    if (i > 0 && sep_len) {
      memcpy(buf + off, sep, sep_len);
      off += sep_len;
    }
    SzString *s = (SzString *)p->head;
    if (s && s->len) {
      memcpy(buf + off, s->data, s->len);
      off += s->len;
    }
    i++;
  }
  buf[off] = '\0';
  SzString *out = sz_string_from_bytes(buf, off);
  sz_free(buf);
  return out;
}
