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

SzList *sz_list_append(SzList *xs, void *x) {
  if (!xs) {
    SzList *n = sz_list_cons(x, NULL);
    sz_release(x);
    return n;
  }
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

SzList *sz_list_map(SzList *xs, SzListMapFn fn, void *env) {
  void *h;
  SzList *n;
  if (!fn)
    sz_panic("sz_list_map(null fn)");
  if (!xs)
    return NULL;
  h = fn(xs->head, env);
  n = sz_list_cons_take(h, sz_list_map(xs->tail, fn, env));
  sz_release(h);
  return n;
}

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
