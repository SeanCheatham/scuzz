#include "scuzz_rt.h"

#include <stdlib.h>
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
 * `x` after the call. Walk `xs` into a reverse acc, then fold onto cons(x). */
SzList *sz_list_append(SzList *xs, void *x) {
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  if (!xs)
    return sz_list_cons(x, NULL);
  for (p = xs; p; p = p->tail)
    acc = sz_list_cons_take(p->head, acc);
  out = sz_list_cons(x, NULL);
  for (p = acc; p; p = p->tail)
    out = sz_list_cons_take(p->head, out);
  sz_release(acc);
  return out;
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

static SzList *list_filter(SzList *xs, SzListPred pred, void *env, int keep,
                           const char *panic) {
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  if (!pred)
    sz_panic(panic);
  for (p = xs; p; p = p->tail) {
    if ((pred(p->head, env) != 0) == keep)
      acc = sz_list_cons_take(p->head, acc);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_list_filter(SzList *xs, SzListPred pred, void *env) {
  return list_filter(xs, pred, env, 1, "sz_list_filter(null pred)");
}

SzList *sz_list_filter_not(SzList *xs, SzListPred pred, void *env) {
  return list_filter(xs, pred, env, 0, "sz_list_filter_not(null pred)");
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
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  int64_t i;
  if (!xs || n <= 0)
    return NULL;
  p = xs;
  for (i = 0; p && i < n; i++, p = p->tail)
    acc = sz_list_cons_take(p->head, acc);
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
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

/* Walk `xs` into a reverse acc, then fold onto a retained `ys`. Empty `xs`
 * retains `ys`. */
SzList *sz_list_concat(SzList *xs, SzList *ys) {
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  if (!xs) {
    sz_retain(ys);
    return ys;
  }
  for (p = xs; p; p = p->tail)
    acc = sz_list_cons_take(p->head, acc);
  sz_retain(ys);
  out = ys;
  for (p = acc; p; p = p->tail)
    out = sz_list_cons_take(p->head, out);
  sz_release(acc);
  return out;
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
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  void *h;
  if (!fn)
    sz_panic("sz_list_map(null fn)");
  /* Mapper returns +1. Cons retains. Drop the mapper ref. */
  for (p = xs; p; p = p->tail) {
    h = fn(p->head, env);
    acc = sz_list_cons_take(h, acc);
    sz_release(h);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
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

SzList *sz_list_slice(SzList *xs, int64_t from, int64_t until) {
  size_t len = sz_list_len(xs);
  SzList *dropped;
  SzList *out;
  int64_t n;
  if (from < 0)
    from = 0;
  if (until < 0)
    until = 0;
  if ((uint64_t)from >= (uint64_t)len || until <= from)
    return NULL;
  if ((uint64_t)until > (uint64_t)len)
    until = (int64_t)len;
  n = until - from;
  dropped = sz_list_drop(xs, from);
  out = sz_list_take(dropped, n);
  sz_release(dropped);
  return out;
}

SzList *sz_list_indices(SzList *xs) {
  return sz_list_range(0, (int64_t)sz_list_len(xs));
}

static SzList *list_two(SzList *a, SzList *b) {
  SzList *inner = sz_list_cons(b, NULL);
  SzList *out;
  sz_release(b);
  out = sz_list_cons_take(a, inner);
  sz_release(a);
  return out;
}

SzList *sz_list_split_at(SzList *xs, int64_t n) {
  SzList *a = sz_list_take(xs, n);
  SzList *b = sz_list_drop(xs, n);
  return list_two(a, b);
}

SzList *sz_list_span(SzList *xs, SzListPred pred, void *env) {
  SzList *a;
  SzList *b;
  if (!pred)
    sz_panic("sz_list_span(null pred)");
  a = sz_list_takewhile(xs, pred, env);
  b = sz_list_dropwhile(xs, pred, env);
  return list_two(a, b);
}

SzList *sz_list_partition(SzList *xs, SzListPred pred, void *env) {
  SzList *a;
  SzList *b;
  if (!pred)
    sz_panic("sz_list_partition(null pred)");
  a = sz_list_filter(xs, pred, env);
  b = sz_list_filter_not(xs, pred, env);
  return list_two(a, b);
}

SzList *sz_list_inits(SzList *xs) {
  size_t n = sz_list_len(xs);
  size_t i;
  SzList *out = NULL;
  SzList *prefix;
  for (i = n + 1; i > 0; i--) {
    prefix = sz_list_take(xs, (int64_t)(i - 1));
    out = sz_list_cons_take(prefix, out);
    sz_release(prefix);
  }
  return out;
}

SzList *sz_list_tails(SzList *xs) {
  SzList *rest;
  if (!xs)
    return sz_list_cons(NULL, NULL);
  rest = sz_list_tails(xs->tail);
  return sz_list_cons_take(xs, rest);
}

static SzPair *elem_pair(void *a, void *b) { return sz_pair_new(a, b); }

SzList *sz_list_zip(SzList *xs, SzList *ys) {
  SzPair *pair;
  SzList *rest;
  SzList *out;
  if (!xs || !ys)
    return NULL;
  pair = elem_pair(xs->head, ys->head);
  rest = sz_list_zip(xs->tail, ys->tail);
  out = sz_list_cons_take(pair, rest);
  sz_release(pair);
  return out;
}

SzList *sz_list_zip_all(SzList *xs, SzList *ys, void *x, void *y) {
  SzPair *pair;
  SzList *rest;
  SzList *out;
  if (!xs && !ys)
    return NULL;
  pair = elem_pair(xs ? xs->head : x, ys ? ys->head : y);
  rest = sz_list_zip_all(xs ? xs->tail : NULL, ys ? ys->tail : NULL, x, y);
  out = sz_list_cons_take(pair, rest);
  sz_release(pair);
  return out;
}

SzPair *sz_list_unzip(SzList *pairs) {
  SzList *as = NULL;
  SzList *bs = NULL;
  SzList *p;
  SzPair *inner;
  SzList *arev;
  SzList *brev;
  SzPair *out;
  for (p = pairs; p; p = p->tail) {
    inner = (SzPair *)p->head;
    if (!inner)
      continue;
    as = sz_list_cons_take(inner->left, as);
    bs = sz_list_cons_take(inner->right, bs);
  }
  arev = sz_list_reverse(as);
  sz_release(as);
  brev = sz_list_reverse(bs);
  sz_release(bs);
  out = sz_pair_new(arev, brev);
  sz_release(arev);
  sz_release(brev);
  return out;
}

int64_t sz_list_contains(SzList *xs, void *x) {
  SzList *p;
  for (p = xs; p; p = p->tail) {
    if (sz_ptr_eq(p->head, x))
      return 1;
  }
  return 0;
}

int64_t sz_list_index_of(SzList *xs, void *x) {
  SzList *p;
  int64_t i = 0;
  for (p = xs; p; p = p->tail) {
    if (sz_ptr_eq(p->head, x))
      return i;
    i++;
  }
  return -1;
}

int64_t sz_list_last_index_of(SzList *xs, void *x) {
  SzList *p;
  int64_t i = 0;
  int64_t hit = -1;
  for (p = xs; p; p = p->tail) {
    if (sz_ptr_eq(p->head, x))
      hit = i;
    i++;
  }
  return hit;
}

SzList *sz_list_distinct(SzList *xs) {
  SzList *acc = NULL;
  SzList *p;
  SzList *rev;
  for (p = xs; p; p = p->tail) {
    if (!sz_list_contains(acc, p->head))
      acc = sz_list_cons_take(p->head, acc);
  }
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return rev;
}

SzList *sz_list_diff(SzList *xs, SzList *ys) {
  SzList *acc = NULL;
  SzList *p;
  SzList *rev;
  for (p = xs; p; p = p->tail) {
    if (!sz_list_contains(ys, p->head))
      acc = sz_list_cons_take(p->head, acc);
  }
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return rev;
}

SzList *sz_list_intersect(SzList *xs, SzList *ys) {
  SzList *acc = NULL;
  SzList *p;
  SzList *rev;
  if (!ys)
    return NULL;
  for (p = xs; p; p = p->tail) {
    if (sz_list_contains(ys, p->head))
      acc = sz_list_cons_take(p->head, acc);
  }
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return rev;
}

SzList *sz_list_transpose(SzList *xss) {
  SzList *p;
  SzList *heads = NULL;
  SzList *tails = NULL;
  SzList *hrev;
  SzList *trev;
  SzList *rest;
  SzList *out;
  SzList *row;
  if (!xss)
    return NULL;
  for (p = xss; p; p = p->tail) {
    if (!p->head)
      return NULL;
  }
  for (p = xss; p; p = p->tail) {
    row = (SzList *)p->head;
    heads = sz_list_cons_take(row->head, heads);
    tails = sz_list_cons_take(row->tail, tails);
  }
  hrev = sz_list_reverse(heads);
  sz_release(heads);
  trev = sz_list_reverse(tails);
  sz_release(tails);
  rest = sz_list_transpose(trev);
  sz_release(trev);
  out = sz_list_cons_take(hrev, rest);
  sz_release(hrev);
  return out;
}

int64_t sz_list_index_where(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  int64_t i = 0;
  if (!pred)
    sz_panic("sz_list_index_where(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env))
      return i;
    i++;
  }
  return -1;
}

int64_t sz_list_last_index_where(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  int64_t i = 0;
  int64_t hit = -1;
  if (!pred)
    sz_panic("sz_list_last_index_where(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env))
      hit = i;
    i++;
  }
  return hit;
}

int64_t sz_list_starts_with(SzList *xs, SzList *prefix) {
  SzList *a = xs;
  SzList *b = prefix;
  while (b) {
    if (!a || !sz_ptr_eq(a->head, b->head))
      return 0;
    a = a->tail;
    b = b->tail;
  }
  return 1;
}

int64_t sz_list_ends_with(SzList *xs, SzList *suffix) {
  size_t n = sz_list_len(xs);
  size_t m = sz_list_len(suffix);
  SzList *p;
  size_t i;
  if (m > n)
    return 0;
  p = xs;
  for (i = 0; i < n - m; i++)
    p = p->tail;
  return sz_list_starts_with(p, suffix);
}

int64_t sz_list_same_elements(SzList *xs, SzList *ys) {
  SzList *a = xs;
  SzList *b = ys;
  while (a && b) {
    if (!sz_ptr_eq(a->head, b->head))
      return 0;
    a = a->tail;
    b = b->tail;
  }
  return a == NULL && b == NULL ? 1 : 0;
}

SzList *sz_list_patch(SzList *xs, int64_t from, SzList *other, int64_t replaced) {
  size_t len = sz_list_len(xs);
  size_t f;
  size_t r;
  size_t skip;
  SzList *left;
  SzList *right;
  SzList *mid;
  SzList *out;
  if (from < 0)
    f = 0;
  else if ((uint64_t)from > (uint64_t)len)
    f = len;
  else
    f = (size_t)from;
  if (replaced < 0)
    r = 0;
  else if ((uint64_t)replaced > (uint64_t)(len - f))
    r = len - f;
  else
    r = (size_t)replaced;
  skip = f + r;
  left = sz_list_take(xs, (int64_t)f);
  right = sz_list_drop(xs, (int64_t)skip);
  mid = sz_list_concat(left, other);
  sz_release(left);
  out = sz_list_concat(mid, right);
  sz_release(mid);
  sz_release(right);
  return out;
}

SzList *sz_list_find_last(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  void *hit = NULL;
  int found = 0;
  if (!pred)
    sz_panic("sz_list_find_last(null pred)");
  for (p = xs; p; p = p->tail) {
    if (pred(p->head, env)) {
      hit = p->head;
      found = 1;
    }
  }
  if (!found)
    return NULL;
  return sz_list_cons(hit, NULL);
}

int64_t sz_list_prefix_length(SzList *xs, SzListPred pred, void *env) {
  SzList *p;
  int64_t n = 0;
  if (!pred)
    sz_panic("sz_list_prefix_length(null pred)");
  for (p = xs; p && pred(p->head, env); p = p->tail)
    n++;
  return n;
}

int64_t sz_list_index_of_slice(SzList *xs, SzList *slice) {
  SzList *p;
  int64_t i = 0;
  if (!slice)
    return 0;
  for (p = xs; p; p = p->tail) {
    if (sz_list_starts_with(p, slice))
      return i;
    i++;
  }
  return -1;
}

int64_t sz_list_last_index_of_slice(SzList *xs, SzList *slice) {
  SzList *p;
  int64_t i = 0;
  int64_t hit = -1;
  if (!slice)
    return (int64_t)sz_list_len(xs);
  for (p = xs; p; p = p->tail) {
    if (sz_list_starts_with(p, slice))
      hit = i;
    i++;
  }
  return hit;
}

int64_t sz_list_segment_length(SzList *xs, SzListPred pred, void *env, int64_t from) {
  SzList *p;
  int64_t i = 0;
  if (!pred)
    sz_panic("sz_list_segment_length(null pred)");
  if (from < 0)
    from = 0;
  for (p = xs; p && i < from; p = p->tail)
    i++;
  return sz_list_prefix_length(p, pred, env);
}

int64_t sz_list_is_defined_at(SzList *xs, int64_t index) {
  SzList *p;
  int64_t i = 0;
  if (index < 0)
    return 0;
  for (p = xs; p; p = p->tail) {
    if (i == index)
      return 1;
    i++;
  }
  return 0;
}

typedef struct {
  void *value;
  int64_t key;
  size_t idx;
} SzSortSlot;

static int str_ord(const SzString *a, const SzString *b) {
  size_t n;
  int c;
  if (a == b)
    return 0;
  if (!a)
    return -1;
  if (!b)
    return 1;
  n = a->len < b->len ? a->len : b->len;
  c = n ? memcmp(a->data, b->data, n) : 0;
  if (c)
    return c;
  if (a->len < b->len)
    return -1;
  if (a->len > b->len)
    return 1;
  return 0;
}

static int cmp_int_slots(const void *a, const void *b) {
  const SzSortSlot *x = (const SzSortSlot *)a;
  const SzSortSlot *y = (const SzSortSlot *)b;
  int64_t kx = sz_unbox_i64(x->value);
  int64_t ky = sz_unbox_i64(y->value);
  if (kx < ky)
    return -1;
  if (kx > ky)
    return 1;
  if (x->idx < y->idx)
    return -1;
  if (x->idx > y->idx)
    return 1;
  return 0;
}

static int cmp_str_slots(const void *a, const void *b) {
  const SzSortSlot *x = (const SzSortSlot *)a;
  const SzSortSlot *y = (const SzSortSlot *)b;
  int c = str_ord((const SzString *)x->value, (const SzString *)y->value);
  if (c)
    return c;
  if (x->idx < y->idx)
    return -1;
  if (x->idx > y->idx)
    return 1;
  return 0;
}

static int cmp_key_slots(const void *a, const void *b) {
  const SzSortSlot *x = (const SzSortSlot *)a;
  const SzSortSlot *y = (const SzSortSlot *)b;
  if (x->key < y->key)
    return -1;
  if (x->key > y->key)
    return 1;
  if (x->idx < y->idx)
    return -1;
  if (x->idx > y->idx)
    return 1;
  return 0;
}

static SzList *sort_slots(SzSortSlot *slots, size_t n,
                          int (*cmp)(const void *, const void *)) {
  SzList *acc = NULL;
  size_t i;
  qsort(slots, n, sizeof(SzSortSlot), cmp);
  for (i = n; i > 0; i--)
    acc = sz_list_cons_take(slots[i - 1].value, acc);
  sz_free(slots);
  return acc;
}

SzList *sz_list_sort(SzList *xs, int64_t as_int) {
  size_t n = sz_list_len(xs);
  SzSortSlot *slots;
  SzList *p;
  size_t i;
  if (!xs)
    return NULL;
  slots = (SzSortSlot *)sz_alloc(n * sizeof(SzSortSlot));
  i = 0;
  for (p = xs; p; p = p->tail) {
    slots[i].value = p->head;
    slots[i].key = 0;
    slots[i].idx = i;
    i++;
  }
  return sort_slots(slots, n, as_int ? cmp_int_slots : cmp_str_slots);
}

SzList *sz_list_sort_by(SzList *xs, SzListMapFn fn, void *env) {
  size_t n = sz_list_len(xs);
  SzSortSlot *slots;
  SzList *p;
  size_t i;
  if (!fn)
    sz_panic("sz_list_sort_by(null fn)");
  if (!xs)
    return NULL;
  slots = (SzSortSlot *)sz_alloc(n * sizeof(SzSortSlot));
  i = 0;
  for (p = xs; p; p = p->tail) {
    void *box = fn(p->head, env);
    slots[i].value = p->head;
    slots[i].key = sz_unbox_i64(box);
    slots[i].idx = i;
    sz_release(box);
    i++;
  }
  return sort_slots(slots, n, cmp_key_slots);
}

static int cell_ord(void *a, void *b, int64_t as_int) {
  if (as_int) {
    int64_t ka = sz_unbox_i64(a);
    int64_t kb = sz_unbox_i64(b);
    if (ka < kb)
      return -1;
    if (ka > kb)
      return 1;
    return 0;
  }
  return str_ord((const SzString *)a, (const SzString *)b);
}

static void *list_extreme(SzList *xs, int64_t as_int, int want_max,
                          const char *empty_msg) {
  SzList *p;
  void *best;
  if (!xs)
    sz_panic(empty_msg);
  best = xs->head;
  for (p = xs->tail; p; p = p->tail) {
    int c = cell_ord(p->head, best, as_int);
    if (want_max ? c > 0 : c < 0)
      best = p->head;
  }
  return best;
}

void *sz_list_max(SzList *xs, int64_t as_int) {
  return list_extreme(xs, as_int, 1, "List.max on empty");
}

void *sz_list_min(SzList *xs, int64_t as_int) {
  return list_extreme(xs, as_int, 0, "List.min on empty");
}

SzMap *sz_list_group_by(SzList *xs, SzListMapFn fn, void *env, int32_t key_kind) {
  SzMap *acc = NULL;
  SzList *p;
  if (!fn)
    sz_panic("sz_list_group_by(null fn)");
  for (p = xs; p; p = p->tail) {
    void *key = fn(p->head, env);
    void *cur = sz_map_get_or(acc, key, NULL);
    SzList *grown;
    SzMap *next;
    if (cur)
      grown = sz_list_append((SzList *)cur, p->head);
    else
      grown = sz_list_cons(p->head, NULL);
    next = sz_map_set(acc, key, grown, key_kind);
    sz_release(grown);
    sz_release(acc);
    sz_release(key);
    acc = next;
  }
  return acc;
}

int64_t sz_list_sum(SzList *xs) {
  int64_t acc = 0;
  SzList *p;
  for (p = xs; p; p = p->tail)
    acc += sz_unbox_i64(p->head);
  return acc;
}

int64_t sz_list_product(SzList *xs) {
  int64_t acc = 1;
  SzList *p;
  for (p = xs; p; p = p->tail)
    acc *= sz_unbox_i64(p->head);
  return acc;
}

void *sz_list_max_by(SzList *xs, SzListMapFn fn, void *env, int64_t want_max) {
  SzList *p;
  void *best;
  int64_t best_k;
  void *box;
  if (!fn)
    sz_panic("sz_list_max_by(null fn)");
  if (!xs)
    sz_panic(want_max ? "List.maxBy on empty" : "List.minBy on empty");
  box = fn(xs->head, env);
  best_k = sz_unbox_i64(box);
  sz_release(box);
  best = xs->head;
  for (p = xs->tail; p; p = p->tail) {
    int64_t k;
    box = fn(p->head, env);
    k = sz_unbox_i64(box);
    sz_release(box);
    if (want_max ? k > best_k : k < best_k) {
      best_k = k;
      best = p->head;
    }
  }
  return best;
}

int64_t sz_list_length_compare(SzList *xs, int64_t n) {
  SzList *p = xs;
  int64_t i = 0;
  if (n < 0)
    return 1;
  while (p && i < n) {
    p = p->tail;
    i++;
  }
  if (i < n)
    return -1;
  if (p)
    return 1;
  return 0;
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
    if (s) {
      if (s->len > SIZE_MAX - total)
        sz_panic("List.join too large");
      total += s->len;
    }
    if (count > 0) {
      if (sep_len > SIZE_MAX - total)
        sz_panic("List.join too large");
      total += sep_len;
    }
    count++;
  }
  if (total == SIZE_MAX)
    sz_panic("List.join too large");
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
