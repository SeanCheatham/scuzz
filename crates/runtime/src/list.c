#include "scuzz_rt.h"

#include <string.h>

/* Linked list: NULL = Nil. Cons cells do not own heads. Signal.list frees
 * unshared spines on set/free. Shared tails (cons onto an existing list)
 * stay. No tracing collector. */

SzList *sz_list_nil(void) { return NULL; }

int sz_list_is_empty(const SzList *xs) { return xs == NULL; }

SzList *sz_list_cons(void *head, SzList *tail) {
  SzList *n = (SzList *)sz_alloc(sizeof(SzList));
  n->head = head;
  n->tail = tail;
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
    acc = sz_list_cons(p->head, acc);
  return acc;
}

SzList *sz_list_append(SzList *xs, void *x) {
  if (!xs)
    return sz_list_cons(x, NULL);
  return sz_list_cons(xs->head, sz_list_append(xs->tail, x));
}

static SzList *sz_list_copy(SzList *xs) {
  if (!xs)
    return NULL;
  return sz_list_cons(xs->head, sz_list_copy(xs->tail));
}

/* Replace the head at `index`. Copy the spine so `xs` stays. Out of range
 * (empty, negative, or past the end) returns `xs`. */
SzList *sz_list_set_at(SzList *xs, int64_t index, void *v) {
  SzList *rest;
  if (!xs || index < 0)
    return xs;
  if (index == 0)
    return sz_list_cons(v, sz_list_copy(xs->tail));
  rest = sz_list_set_at(xs->tail, index - 1, v);
  if (rest == xs->tail)
    return xs;
  return sz_list_cons(xs->head, rest);
}

SzList *sz_list_filter(SzList *xs, SzListPred pred, void *env) {
  if (!pred)
    sz_panic("sz_list_filter(null pred)");
  if (!xs)
    return NULL;
  if (pred(xs->head, env))
    return sz_list_cons(xs->head, sz_list_filter(xs->tail, pred, env));
  return sz_list_filter(xs->tail, pred, env);
}

SzList *sz_list_map(SzList *xs, SzListMapFn fn, void *env) {
  if (!fn)
    sz_panic("sz_list_map(null fn)");
  if (!xs)
    return NULL;
  return sz_list_cons(fn(xs->head, env), sz_list_map(xs->tail, fn, env));
}

void sz_list_free(SzList *xs) {
  while (xs) {
    SzList *n = xs->tail;
    sz_free(xs);
    xs = n;
  }
}

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
