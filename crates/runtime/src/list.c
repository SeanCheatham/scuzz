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

int64_t sz_list_len(const SzList *xs) {
  int64_t n = 0;
  for (const SzList *p = xs; p; p = p->tail)
    n++;
  return n;
}

void *sz_list_at(const SzList *xs, int64_t index) {
  const SzList *p = xs;
  int64_t i = 0;
  if (index < 0)
    sz_panic("List.at out of bounds");
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

/* Replace the head at `index`. Copy the spine so `xs` is not mutated.
 * Out of range (empty, negative, or past the end) returns `xs` with an extra
 * retain so the caller can drop their ref. */
SzList *sz_list_set_at(SzList *xs, int64_t index, void *v) {
  SzList *prefix;
  SzList *suffix;
  SzList *mid;
  SzList *out;
  if (!xs || index < 0 || index >= sz_list_len(xs)) {
    sz_retain(xs);
    return xs;
  }
  prefix = sz_list_take(xs, index);
  suffix = sz_list_drop(xs, index + 1);
  mid = sz_list_cons_take(v, suffix);
  out = sz_list_concat(prefix, mid);
  sz_release(prefix);
  sz_release(mid);
  return out;
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
  int64_t len;
  if (!xs || n <= 0)
    return NULL;
  len = sz_list_len(xs);
  if (n >= len)
    return sz_list_drop(xs, 0);
  return sz_list_drop(xs, len - n);
}

SzList *sz_list_drop_right(SzList *xs, int64_t n) {
  int64_t len;
  if (!xs)
    return NULL;
  if (n <= 0)
    return sz_list_drop(xs, 0);
  len = sz_list_len(xs);
  if (n >= len)
    return NULL;
  return sz_list_take(xs, len - n);
}

SzList *sz_list_init(SzList *xs) { return sz_list_drop_right(xs, 1); }

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
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  if (!pred)
    sz_panic("sz_list_takewhile(null pred)");
  for (p = xs; p; p = p->tail) {
    if (!pred(p->head, env))
      break;
    acc = sz_list_cons_take(p->head, acc);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
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

/* Concatenate inners of a reversed outer spine. `concat(inner, out)` is
 * linear in the inner length, so the whole walk is linear in total cells. */
static SzList *flatten_from_rev(SzList *rev) {
  SzList *out = NULL;
  SzList *p;
  SzList *next;
  for (p = rev; p; p = p->tail) {
    next = sz_list_concat((SzList *)p->head, out);
    sz_release(out);
    out = next;
  }
  return out;
}

SzList *sz_list_flatten(SzList *xss) {
  SzList *rev = sz_list_reverse(xss);
  SzList *out = flatten_from_rev(rev);
  sz_release(rev);
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
  SzList *chunks = NULL;
  SzList *p;
  SzList *chunk;
  SzList *out;
  if (!fn)
    sz_panic("sz_list_flat_map(null fn)");
  /* Mapper returns +1 list. Cons retains. Drop the mapper list. */
  for (p = xs; p; p = p->tail) {
    chunk = (SzList *)fn(p->head, env);
    chunks = sz_list_cons_take(chunk, chunks);
    sz_release(chunk);
  }
  out = flatten_from_rev(chunks);
  sz_release(chunks);
  return out;
}

SzList *sz_list_pad_to(SzList *xs, int64_t n, void *x) {
  int64_t len = sz_list_len(xs);
  SzList *suffix;
  SzList *out;
  if (n < 0)
    n = 0;
  if (n <= len)
    return sz_list_drop(xs, 0);
  suffix = sz_list_fill(n - len, x);
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
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  int first = 1;
  if (!xs)
    return NULL;
  if (!xs->tail) {
    sz_retain(xs);
    return xs;
  }
  for (p = xs; p; p = p->tail) {
    if (!first)
      acc = sz_list_cons_take(x, acc);
    first = 0;
    acc = sz_list_cons_take(p->head, acc);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_list_grouped(SzList *xs, int64_t n) {
  SzList *acc = NULL;
  SzList *rest;
  SzList *chunk;
  SzList *next;
  SzList *out;
  if (!xs || n <= 0)
    return NULL;
  rest = sz_list_drop(xs, 0);
  while (rest) {
    chunk = sz_list_take(rest, n);
    next = sz_list_drop(rest, n);
    sz_release(rest);
    rest = next;
    acc = sz_list_cons_take(chunk, acc);
    sz_release(chunk);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_list_sliding(SzList *xs, int64_t n) {
  SzList *acc = NULL;
  SzList *p;
  SzList *window;
  SzList *out;
  int64_t remaining;
  if (!xs || n <= 0)
    return NULL;
  remaining = sz_list_len(xs);
  if (n > remaining)
    return NULL;
  p = xs;
  while (remaining >= n) {
    window = sz_list_take(p, n);
    acc = sz_list_cons_take(window, acc);
    sz_release(window);
    p = p->tail;
    remaining--;
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_list_slice(SzList *xs, int64_t from, int64_t until) {
  int64_t len = sz_list_len(xs);
  SzList *dropped;
  SzList *out;
  int64_t n;
  if (from < 0)
    from = 0;
  if (until < 0)
    until = 0;
  if (from >= len || until <= from)
    return NULL;
  if (until > len)
    until = len;
  n = until - from;
  dropped = sz_list_drop(xs, from);
  out = sz_list_take(dropped, n);
  sz_release(dropped);
  return out;
}

SzList *sz_list_indices(SzList *xs) { return sz_list_range(0, sz_list_len(xs)); }

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
  int64_t n = sz_list_len(xs);
  int64_t i;
  SzList *out = NULL;
  SzList *prefix;
  for (i = n + 1; i > 0; i--) {
    prefix = sz_list_take(xs, i - 1);
    out = sz_list_cons_take(prefix, out);
    sz_release(prefix);
  }
  return out;
}

SzList *sz_list_tails(SzList *xs) {
  SzList *acc = NULL;
  SzList *p;
  SzList *out;
  for (p = xs; p; p = p->tail)
    acc = sz_list_cons_take(p, acc);
  out = sz_list_cons(NULL, NULL);
  for (p = acc; p; p = p->tail)
    out = sz_list_cons_take(p->head, out);
  sz_release(acc);
  return out;
}

static SzPair *elem_pair(void *a, void *b) { return sz_pair_new(a, b); }

SzList *sz_list_zip(SzList *xs, SzList *ys) {
  SzList *acc = NULL;
  SzList *a = xs;
  SzList *b = ys;
  SzPair *pair;
  SzList *out;
  while (a && b) {
    pair = elem_pair(a->head, b->head);
    acc = sz_list_cons_take(pair, acc);
    sz_release(pair);
    a = a->tail;
    b = b->tail;
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_list_zip_all(SzList *xs, SzList *ys, void *x, void *y) {
  SzList *acc = NULL;
  SzList *a = xs;
  SzList *b = ys;
  SzPair *pair;
  SzList *out;
  while (a || b) {
    pair = elem_pair(a ? a->head : x, b ? b->head : y);
    acc = sz_list_cons_take(pair, acc);
    sz_release(pair);
    if (a)
      a = a->tail;
    if (b)
      b = b->tail;
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
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

SzList *sz_list_zip_with_index(SzList *xs) {
  SzList *p;
  SzList *acc = NULL;
  SzList *out;
  int64_t i = 0;
  void *box;
  SzPair *pair;
  for (p = xs; p; p = p->tail) {
    box = sz_box_i64(i);
    pair = sz_pair_new(box, p->head);
    sz_release(box);
    acc = sz_list_cons_take(pair, acc);
    sz_release(pair);
    i++;
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

void *sz_list_fold_left(SzList *xs, void *z, SzListMapFn fn, void *env) {
  void *acc;
  SzList *p;
  SzPair *pack;
  void *next;
  if (!fn)
    sz_panic("sz_list_fold_left(null fn)");
  sz_retain(z);
  acc = z;
  for (p = xs; p; p = p->tail) {
    pack = sz_pair_new(acc, p->head);
    next = fn(pack, env);
    sz_release(pack);
    sz_release(acc);
    acc = next;
  }
  return acc;
}

void *sz_list_fold_right(SzList *xs, void *z, SzListMapFn fn, void *env) {
  SzList *rev;
  void *acc;
  SzList *p;
  SzPair *pack;
  void *next;
  if (!fn)
    sz_panic("sz_list_fold_right(null fn)");
  rev = sz_list_reverse(xs);
  sz_retain(z);
  acc = z;
  for (p = rev; p; p = p->tail) {
    pack = sz_pair_new(p->head, acc);
    next = fn(pack, env);
    sz_release(pack);
    sz_release(acc);
    acc = next;
  }
  sz_release(rev);
  return acc;
}

SzList *sz_list_scan_left(SzList *xs, void *z, SzListMapFn fn, void *env) {
  SzList *acc_rev = NULL;
  SzList *p;
  SzPair *pack;
  void *acc;
  void *next;
  SzList *out;
  if (!fn)
    sz_panic("sz_list_scan_left(null fn)");
  sz_retain(z);
  acc = z;
  acc_rev = sz_list_cons_take(acc, acc_rev);
  for (p = xs; p; p = p->tail) {
    pack = sz_pair_new(acc, p->head);
    next = fn(pack, env);
    sz_release(pack);
    sz_release(acc);
    acc = next;
    acc_rev = sz_list_cons_take(acc, acc_rev);
  }
  sz_release(acc);
  out = sz_list_reverse(acc_rev);
  sz_release(acc_rev);
  return out;
}

SzList *sz_list_scan_right(SzList *xs, void *z, SzListMapFn fn, void *env) {
  SzList *rev;
  SzList *acc_rev = NULL;
  SzList *p;
  SzPair *pack;
  void *acc;
  void *next;
  if (!fn)
    sz_panic("sz_list_scan_right(null fn)");
  rev = sz_list_reverse(xs);
  sz_retain(z);
  acc = z;
  acc_rev = sz_list_cons_take(acc, acc_rev);
  for (p = rev; p; p = p->tail) {
    pack = sz_pair_new(p->head, acc);
    next = fn(pack, env);
    sz_release(pack);
    sz_release(acc);
    acc = next;
    acc_rev = sz_list_cons_take(acc, acc_rev);
  }
  sz_release(acc);
  sz_release(rev);
  return acc_rev;
}

void *sz_list_reduce_left(SzList *xs, SzListMapFn fn, void *env) {
  if (!xs)
    sz_panic("List.reduceLeft on empty");
  if (!fn)
    sz_panic("sz_list_reduce_left(null fn)");
  return sz_list_fold_left(xs->tail, xs->head, fn, env);
}

void *sz_list_reduce_right(SzList *xs, SzListMapFn fn, void *env) {
  SzList *rev;
  void *acc;
  SzList *p;
  SzPair *pack;
  void *next;
  if (!xs)
    sz_panic("List.reduceRight on empty");
  if (!fn)
    sz_panic("sz_list_reduce_right(null fn)");
  rev = sz_list_reverse(xs);
  sz_retain(rev->head);
  acc = rev->head;
  for (p = rev->tail; p; p = p->tail) {
    pack = sz_pair_new(p->head, acc);
    next = fn(pack, env);
    sz_release(pack);
    sz_release(acc);
    acc = next;
  }
  sz_release(rev);
  return acc;
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

SzList *sz_list_distinct_by(SzList *xs, SzListMapFn fn, void *env,
                            int32_t key_kind) {
  SzMap *seen = NULL;
  SzList *acc = NULL;
  SzList *p;
  SzList *rev;
  if (!fn)
    sz_panic("sz_list_distinct_by(null fn)");
  for (p = xs; p; p = p->tail) {
    void *key = fn(p->head, env);
    if (sz_map_contains(seen, key) == 0) {
      SzMap *next = sz_map_set(seen, key, NULL, key_kind);
      sz_release(seen);
      seen = next;
      acc = sz_list_cons_take(p->head, acc);
    }
    sz_release(key);
  }
  sz_release(seen);
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return rev;
}

SzMap *sz_list_to_map(SzList *pairs) {
  SzMap *acc = NULL;
  SzList *p;
  int32_t kind = 1;
  if (pairs && pairs->head) {
    SzPair *first = (SzPair *)pairs->head;
    if (first && first->left)
      kind = sz_map_infer_key_kind(first->left);
  }
  for (p = pairs; p; p = p->tail) {
    SzPair *pair = (SzPair *)p->head;
    SzMap *next;
    if (!pair)
      continue;
    next = sz_map_set(acc, pair->left, pair->right, kind);
    sz_release(acc);
    acc = next;
  }
  return acc;
}

SzMap *sz_list_to_set(SzList *xs, int32_t key_kind) {
  SzMap *acc = NULL;
  SzList *p;
  for (p = xs; p; p = p->tail) {
    SzMap *next = sz_map_set(acc, p->head, NULL, key_kind);
    sz_release(acc);
    acc = next;
  }
  return acc;
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
  SzList *rows;
  SzList *acc = NULL;
  SzList *p;
  SzList *heads;
  SzList *next_rows;
  SzList *hrev;
  SzList *row;
  SzList *out;
  int empty;
  if (!xss)
    return NULL;
  sz_retain(xss);
  rows = xss;
  while (rows) {
    empty = 0;
    for (p = rows; p; p = p->tail) {
      if (!p->head) {
        empty = 1;
        break;
      }
    }
    if (empty)
      break;
    heads = NULL;
    next_rows = NULL;
    for (p = rows; p; p = p->tail) {
      row = (SzList *)p->head;
      heads = sz_list_cons_take(row->head, heads);
      next_rows = sz_list_cons_take(row->tail, next_rows);
    }
    hrev = sz_list_reverse(heads);
    sz_release(heads);
    acc = sz_list_cons_take(hrev, acc);
    sz_release(hrev);
    sz_release(rows);
    rows = sz_list_reverse(next_rows);
    sz_release(next_rows);
  }
  sz_release(rows);
  out = sz_list_reverse(acc);
  sz_release(acc);
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
  int64_t n = sz_list_len(xs);
  int64_t m = sz_list_len(suffix);
  SzList *p;
  int64_t i;
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
  int64_t len = sz_list_len(xs);
  int64_t f;
  int64_t r;
  int64_t skip;
  SzList *left;
  SzList *right;
  SzList *mid;
  SzList *out;
  if (from < 0)
    f = 0;
  else if (from > len)
    f = len;
  else
    f = from;
  if (replaced < 0)
    r = 0;
  else if (replaced > len - f)
    r = len - f;
  else
    r = replaced;
  skip = f + r;
  left = sz_list_take(xs, f);
  right = sz_list_drop(xs, skip);
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
    return sz_list_len(xs);
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
  int64_t n = sz_list_len(xs);
  SzSortSlot *slots;
  SzList *p;
  size_t i;
  if (!xs)
    return NULL;
  slots = (SzSortSlot *)sz_alloc((size_t)n * sizeof(SzSortSlot));
  i = 0;
  for (p = xs; p; p = p->tail) {
    slots[i].value = p->head;
    slots[i].key = 0;
    slots[i].idx = i;
    i++;
  }
  return sort_slots(slots, (size_t)n, as_int ? cmp_int_slots : cmp_str_slots);
}

SzList *sz_list_sort_by(SzList *xs, SzListMapFn fn, void *env) {
  int64_t n = sz_list_len(xs);
  SzSortSlot *slots;
  SzList *p;
  size_t i;
  if (!fn)
    sz_panic("sz_list_sort_by(null fn)");
  if (!xs)
    return NULL;
  slots = (SzSortSlot *)sz_alloc((size_t)n * sizeof(SzSortSlot));
  i = 0;
  for (p = xs; p; p = p->tail) {
    void *box = fn(p->head, env);
    slots[i].value = p->head;
    slots[i].key = sz_unbox_i64(box);
    slots[i].idx = i;
    sz_release(box);
    i++;
  }
  return sort_slots(slots, (size_t)n, cmp_key_slots);
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

static void *list_reverse_value(void *head, void *env) {
  (void)env;
  return sz_list_reverse((SzList *)head);
}

SzMap *sz_list_group_by(SzList *xs, SzListMapFn fn, void *env, int32_t key_kind) {
  SzMap *acc = NULL;
  SzMap *out;
  SzList *p;
  if (!fn)
    sz_panic("sz_list_group_by(null fn)");
  for (p = xs; p; p = p->tail) {
    void *key = fn(p->head, env);
    void *cur = sz_map_get_or(acc, key, NULL);
    SzList *grown = sz_list_cons(p->head, (SzList *)cur);
    SzMap *next = sz_map_set(acc, key, grown, key_kind);
    sz_release(grown);
    sz_release(acc);
    sz_release(key);
    acc = next;
  }
  out = sz_map_map_values(acc, list_reverse_value, NULL);
  sz_release(acc);
  return out;
}

int64_t sz_list_sum(SzList *xs) {
  uint64_t acc = 0;
  SzList *p;
  for (p = xs; p; p = p->tail)
    acc += (uint64_t)sz_unbox_i64(p->head);
  return (int64_t)acc;
}

int64_t sz_list_product(SzList *xs) {
  uint64_t acc = 1;
  SzList *p;
  for (p = xs; p; p = p->tail)
    acc *= (uint64_t)sz_unbox_i64(p->head);
  return (int64_t)acc;
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
