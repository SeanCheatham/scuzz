#include "scalui_rt.h"

#include <string.h>

/* Linked list: NULL = Nil. Cons cells own neither head nor deep tail frees
 * for GC-v0 (arena-ish malloc; no collection). */

SuList *su_list_nil(void) { return NULL; }

int su_list_is_empty(const SuList *xs) { return xs == NULL; }

SuList *su_list_cons(void *head, SuList *tail) {
  SuList *n = (SuList *)su_alloc(sizeof(SuList));
  n->head = head;
  n->tail = tail;
  return n;
}

void *su_list_head(const SuList *xs) {
  if (!xs)
    su_panic("List.head on empty");
  return xs->head;
}

SuList *su_list_tail(const SuList *xs) {
  if (!xs)
    su_panic("List.tail on empty");
  return xs->tail;
}

size_t su_list_len(const SuList *xs) {
  size_t n = 0;
  for (const SuList *p = xs; p; p = p->tail)
    n++;
  return n;
}

void *su_list_at(const SuList *xs, size_t index) {
  const SuList *p = xs;
  size_t i = 0;
  while (p) {
    if (i == index)
      return p->head;
    p = p->tail;
    i++;
  }
  su_panic("List.at out of bounds");
  return NULL;
}

SuList *su_list_reverse(SuList *xs) {
  SuList *acc = NULL;
  for (SuList *p = xs; p; p = p->tail)
    acc = su_list_cons(p->head, acc);
  return acc;
}

SuList *su_list_append(SuList *xs, void *x) {
  if (!xs)
    return su_list_cons(x, NULL);
  return su_list_cons(xs->head, su_list_append(xs->tail, x));
}

SuString *su_list_join(const SuList *xs, const char *sep) {
  if (!sep)
    sep = "";
  size_t sep_len = strlen(sep);
  size_t total = 0;
  size_t count = 0;
  for (const SuList *p = xs; p; p = p->tail) {
    SuString *s = (SuString *)p->head;
    if (s)
      total += s->len;
    if (count > 0)
      total += sep_len;
    count++;
  }
  char *buf = (char *)su_alloc(total + 1);
  size_t off = 0;
  size_t i = 0;
  for (const SuList *p = xs; p; p = p->tail) {
    if (i > 0 && sep_len) {
      memcpy(buf + off, sep, sep_len);
      off += sep_len;
    }
    SuString *s = (SuString *)p->head;
    if (s && s->len) {
      memcpy(buf + off, s->data, s->len);
      off += s->len;
    }
    i++;
  }
  buf[off] = '\0';
  SuString *out = su_string_from_bytes(buf, off);
  su_free(buf);
  return out;
}
