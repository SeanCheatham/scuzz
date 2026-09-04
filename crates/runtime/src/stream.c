#include "scuzz_rt.h"
#include "rt_util.h"

#include <string.h>

static SzStream *st_keep(SzStream *s) {
  if (!s)
    return sz_stream_nil();
  sz_retain(s);
  return s;
}

static SzStream *st_new(int tag, void *left, void *right, void *env) {
  SzStream *s = (SzStream *)sz_rc_alloc(sizeof(SzStream), SZ_RC_STREAM);
  memset(s, 0, sizeof(SzStream));
  s->tag = tag;
  s->left = left;
  s->right = right;
  s->env = env;
  return s;
}

SzStream *sz_stream_nil(void) { return st_new(SZ_ST_NIL, NULL, NULL, NULL); }

SzStream *sz_stream_emit(void *value) {
  sz_retain(value);
  return st_new(SZ_ST_CONS, value, sz_stream_nil(), NULL);
}

SzStream *sz_stream_emits(SzList *xs) {
  SzStream *s = sz_stream_nil();
  SzList *rev = sz_list_reverse(xs);
  SzList *p = rev;
  while (!sz_list_is_empty(p)) {
    void *head = sz_list_head(p);
    sz_retain(head);
    s = st_new(SZ_ST_CONS, head, s, NULL);
    p = sz_list_tail(p);
  }
  sz_release(rev);
  return s;
}

SzStream *sz_stream_eval(SzIo *io) {
  if (!io)
    sz_panic("sz_stream_eval(null)");
  sz_retain(io);
  return st_new(SZ_ST_EVAL, io, sz_stream_nil(), NULL);
}

SzStream *sz_stream_concat(SzStream *left, SzStream *right) {
  return st_new(SZ_ST_CONCAT, st_keep(left), st_keep(right), NULL);
}

SzStream *sz_stream_evalmap(SzStream *inner, SzCont f, void *env) {
  if (!f)
    sz_panic("sz_stream_evalmap(null)");
  sz_retain(env);
  return st_new(SZ_ST_EVALMAP, st_keep(inner), (void *)f, env);
}

SzStream *sz_stream_filter(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_filter(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_FILTER, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_map(SzStream *inner, SzStreamMapFn f, void *env) {
  if (!f)
    sz_panic("sz_stream_map(null fn)");
  sz_retain(env);
  return st_new(SZ_ST_MAP, st_keep(inner), (void *)f, env);
}

SzStream *sz_stream_takewhile(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_takewhile(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_TAKEWHILE, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_dropwhile(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_dropwhile(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_DROPWHILE, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_find(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_find(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_FIND, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_take(SzStream *inner, int64_t n) {
  if (n <= 0)
    return sz_stream_nil();
  return st_new(SZ_ST_TAKE, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_drop(SzStream *inner, int64_t n) {
  if (n < 0)
    n = 0;
  return st_new(SZ_ST_DROP, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_range(int64_t from, int64_t until) {
  if (until <= from)
    return sz_stream_nil();
  return st_new(SZ_ST_RANGE, NULL, (void *)(intptr_t)until,
                (void *)(intptr_t)from);
}

SzStream *sz_stream_repeat_n(SzStream *inner, int64_t n) {
  SzStream *s;
  int64_t i;
  if (n <= 0)
    return sz_stream_nil();
  s = st_keep(inner);
  for (i = 1; i < n; i++)
    s = st_new(SZ_ST_CONCAT, s, st_keep(inner), NULL);
  return s;
}

SzStream *sz_stream_zip(SzStream *left, SzStream *right) {
  return st_new(SZ_ST_ZIP, st_keep(left), st_keep(right), NULL);
}

SzStream *sz_stream_interleave(SzStream *left, SzStream *right) {
  return st_new(SZ_ST_INTERLEAVE, st_keep(left), st_keep(right), NULL);
}

SzStream *sz_stream_zip_with_index(SzStream *inner) {
  return st_new(SZ_ST_ZIPIDX, st_keep(inner), NULL, NULL);
}

SzStream *sz_stream_intersperse(SzStream *inner, void *sep) {
  sz_retain(sep);
  return st_new(SZ_ST_INTERSPERSE, st_keep(inner), sep, NULL);
}

SzStream *sz_stream_grouped(SzStream *inner, int64_t n) {
  if (n <= 0)
    return sz_stream_nil();
  return st_new(SZ_ST_GROUPED, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_flatten(SzStream *inner) {
  return st_new(SZ_ST_FLATTEN, st_keep(inner), NULL, NULL);
}

SzStream *sz_stream_flatmap(SzStream *inner, SzStreamMapFn f, void *env) {
  if (!f)
    sz_panic("sz_stream_flatmap(null fn)");
  sz_retain(env);
  return st_new(SZ_ST_FLATMAP, st_keep(inner), (void *)f, env);
}

SzStream *sz_stream_scan(SzStream *inner, void *z, SzStreamMapFn f, void *env) {
  SzPair *pack;
  if (!f)
    sz_panic("sz_stream_scan(null fn)");
  pack = sz_pair_new(z, env);
  return st_new(SZ_ST_SCAN, st_keep(inner), (void *)f, pack);
}

SzStream *sz_stream_changes(SzStream *inner) {
  return st_new(SZ_ST_CHANGES, st_keep(inner), NULL, NULL);
}

SzStream *sz_stream_filter_not(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_filter_not(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_FILTERNOT, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_map_concat(SzStream *inner, SzStreamMapFn f, void *env) {
  if (!f)
    sz_panic("sz_stream_map_concat(null fn)");
  sz_retain(env);
  return st_new(SZ_ST_MAPCONCAT, st_keep(inner), (void *)f, env);
}

SzStream *sz_stream_zip_with(SzStream *left, SzStream *right, SzStreamMapFn f,
                            void *env) {
  if (!f)
    sz_panic("sz_stream_zip_with(null fn)");
  return st_new(SZ_ST_ZIPWITH, st_keep(left), st_keep(right),
                sz_pair_new((void *)f, env));
}

SzStream *sz_stream_zip_all(SzStream *left, SzStream *right, void *pad_left,
                           void *pad_right) {
  return st_new(SZ_ST_ZIPALL, st_keep(left), st_keep(right),
                sz_pair_new(pad_left, pad_right));
}

SzStream *sz_stream_or_else(SzStream *left, SzStream *right) {
  return st_new(SZ_ST_ORELSE, st_keep(left), st_keep(right), NULL);
}

SzStream *sz_stream_sliding(SzStream *inner, int64_t n) {
  if (n <= 0)
    return sz_stream_nil();
  return st_new(SZ_ST_SLIDING, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_take_right(SzStream *inner, int64_t n) {
  if (n <= 0)
    return sz_stream_nil();
  return st_new(SZ_ST_TAKERIGHT, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_drop_right(SzStream *inner, int64_t n) {
  if (n <= 0)
    return st_keep(inner);
  return st_new(SZ_ST_DROPRIGHT, st_keep(inner), NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_find_last(SzStream *inner, SzStreamPred pred, void *env) {
  if (!pred)
    sz_panic("sz_stream_find_last(null pred)");
  sz_retain(env);
  return st_new(SZ_ST_FINDLAST, st_keep(inner), (void *)pred, env);
}

SzStream *sz_stream_evaltap(SzStream *inner, SzCont f, void *env) {
  if (!f)
    sz_panic("sz_stream_evaltap(null fn)");
  sz_retain(env);
  return st_new(SZ_ST_EVALTAP, st_keep(inner), (void *)f, env);
}

#define SZ_UNFOLD_CAP 65536

SzStream *sz_stream_iterate(void *z, int64_t n, SzStreamMapFn f, void *env) {
  void *nb;
  SzPair *pack;
  if (!f)
    sz_panic("sz_stream_iterate(null fn)");
  if (n <= 0)
    return sz_stream_nil();
  sz_retain(z);
  nb = sz_box_i64(n);
  pack = sz_pair_new(nb, env);
  sz_release(nb);
  return st_new(SZ_ST_ITERATE, z, (void *)f, pack);
}

SzStream *sz_stream_unfold(void *z, SzStreamMapFn f, void *env) {
  if (!f)
    sz_panic("sz_stream_unfold(null fn)");
  sz_retain(z);
  sz_retain(env);
  return st_new(SZ_ST_UNFOLD, z, (void *)f, env);
}

typedef struct StEval {
  SzStream *tail;
  SzList *acc;
  int64_t remain;
} StEval;

typedef struct StConcat {
  SzStream *right;
  int64_t remain;
  int64_t acc_len;
} StConcat;

typedef struct StDrop {
  int64_t n;
  int64_t acc_len;
} StDrop;

typedef struct StFilter {
  SzStreamPred pred;
  void *penv;
  int64_t remain;
  int64_t acc_len;
} StFilter;

typedef struct StMap {
  SzCont f;
  void *fenv;
  SzList *outer_acc;
  SzList *xs;
  SzList *xs_root;
  void *cur;
} StMap;

typedef struct StSyncMap {
  SzStreamMapFn f;
  void *fenv;
  SzList *outer_acc;
} StSyncMap;

typedef struct StTW {
  SzStreamPred pred;
  void *penv;
  int stopped;
} StTW;

typedef struct StTWEval {
  SzStream *tail;
  SzList *acc;
  int64_t remain;
  SzStreamPred pred;
  void *penv;
  int *stopped;
} StTWEval;

typedef struct StTWConcat {
  SzStream *right;
  int64_t remain;
  int64_t acc_len;
  SzStreamPred pred;
  void *penv;
  int *stopped;
} StTWConcat;

typedef struct StTWCut {
  SzStreamPred pred;
  void *penv;
  int64_t remain;
  int64_t acc_len;
  int *stopped;
} StTWCut;

typedef struct StDropWhile {
  SzStreamPred pred;
  void *penv;
  int64_t remain;
  int64_t acc_len;
} StDropWhile;

/* remain < 0 means unlimited. */
static SzIo *compile_into(SzStream *s, SzList *acc, int64_t remain);
static SzIo *lift_into(SzStream *s, SzList *acc, int64_t remain);
static SzIo *zip_into(SzStream *s, SzList *acc, int64_t remain);
static SzIo *takewhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv, int *stopped);
static SzIo *find_into(SzStream *s, SzList *acc, int64_t remain,
                       SzStreamPred pred, void *penv, int *found);
static SzIo *filter_into(SzStream *s, SzList *acc, int64_t remain,
                         SzStreamPred pred, void *penv);
static SzIo *dropwhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv);
static SzIo *after_or_else(void *acc, void *env);
static SzIo *after_tap_inner(void *inner_acc, void *env);
static SzIo *mapconcat_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamMapFn f, void *fenv);
static SzIo *changes_into(SzStream *s, SzList *acc, int64_t remain, void *prev,
                          int have);
static SzIo *flatmap_into(SzStream *s, SzList *acc, int64_t remain,
                          SzStreamMapFn f, void *fenv);
static int64_t filter_not_pred(void *v, void *env);

static int64_t remain_dec(int64_t remain) {
  return remain < 0 ? remain : remain - 1;
}

/* Reverse a unique spine. Drop xs. */
static SzList *reverse_take(SzList *xs) {
  SzList *rev = sz_list_reverse(xs);
  sz_release(xs);
  return rev;
}

/* Cons, then drop the unique ref to tail. */
static SzList *cons_take(void *head, SzList *tail) {
  SzList *n = sz_list_cons(head, tail);
  sz_release(tail);
  return n;
}

static SzIo *after_eval(void *value, void *env) {
  StEval *st = (StEval *)env;
  SzList *acc = cons_take(value, st->acc);
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
  sz_release(value);
  sz_free(st);
  return compile_into(tail, acc, remain);
}

static SzIo *after_concat(void *acc, void *env) {
  StConcat *st = (StConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  int64_t added;
  sz_free(st);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  return compile_into(right, (SzList *)acc, remain);
}

static SzIo *after_or_else(void *acc, void *env) {
  StConcat *st = (StConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  int64_t added;
  sz_free(st);
  added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
  /* Empty left is not a fail. Pull the right stream. */
  if (added > 0)
    return pure_drop(acc);
  return compile_into(right, (SzList *)acc, remain);
}

/* acc is newest-first. Drop the oldest n of the segment added after acc_len. */
static SzList *drop_added(SzList *acc, int64_t acc_len, int64_t n) {
  int64_t added = (int64_t)sz_list_len(acc) - acc_len;
  int64_t keep;
  int64_t i;
  SzList *kept_rev = sz_list_nil();
  SzList *kept_root;
  SzList *p = acc;
  SzList *out;
  if (n < 0)
    n = 0;
  if (added < 0)
    added = 0;
  keep = added - n;
  if (keep < 0)
    keep = 0;
  for (i = 0; i < keep; i++) {
    kept_rev = cons_take(sz_list_head(p), kept_rev);
    p = sz_list_tail(p);
  }
  for (i = 0; i < added - keep; i++)
    p = sz_list_tail(p);
  sz_retain(p);
  sz_release(acc);
  kept_root = kept_rev;
  out = p;
  while (!sz_list_is_empty(kept_rev)) {
    out = cons_take(sz_list_head(kept_rev), out);
    kept_rev = sz_list_tail(kept_rev);
  }
  sz_release(kept_root);
  return out;
}

static SzIo *after_drop(void *acc, void *env) {
  StDrop *st = (StDrop *)env;
  SzList *out = drop_added((SzList *)acc, st->acc_len, st->n);
  sz_free(st);
  return pure_drop(out);
}

/* acc is newest-first. Keep matching items from the segment added after acc_len.
   If remain >= 0, keep only the oldest `remain` matches. */
static SzList *filter_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                            void *penv, int64_t remain) {
  int64_t added = (int64_t)sz_list_len(acc) - acc_len;
  int64_t i;
  SzList *p = acc;
  SzList *oldest_first = sz_list_nil();
  SzList *oldest_root;
  SzList *kept_nf = sz_list_nil();
  SzList *kept_of;
  SzList *kept_of_root;
  SzList *out;
  int64_t nkeep;
  int64_t n;
  if (added < 0)
    added = 0;
  for (i = 0; i < added; i++) {
    oldest_first = cons_take(sz_list_head(p), oldest_first);
    p = sz_list_tail(p);
  }
  oldest_root = oldest_first;
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) != 0)
      kept_nf = cons_take(h, kept_nf);
  }
  sz_release(oldest_root);
  kept_of = reverse_take(kept_nf);
  n = (int64_t)sz_list_len(kept_of);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  if (nkeep < 0)
    nkeep = 0;
  sz_retain(p);
  sz_release(acc);
  out = p;
  kept_of_root = kept_of;
  for (i = 0; i < nkeep; i++) {
    out = cons_take(sz_list_head(kept_of), out);
    kept_of = sz_list_tail(kept_of);
  }
  sz_release(kept_of_root);
  return out;
}

static SzIo *after_filter(void *acc, void *env) {
  StFilter *st = (StFilter *)env;
  SzList *out =
      filter_added((SzList *)acc, st->acc_len, st->pred, st->penv, st->remain);
  sz_free(st);
  return pure_drop(out);
}

static SzIo *fold_evalmap(SzList *xs, StMap *st);

static SzIo *after_map_one(void *value, void *env) {
  StMap *st = (StMap *)env;
  st->outer_acc = cons_take(value, st->outer_acc);
  sz_release(value);
  return fold_evalmap(st->xs, st);
}

static SzIo *fold_evalmap(SzList *xs, StMap *st) {
  if (sz_list_is_empty(xs)) {
    SzList *acc = st->outer_acc;
    sz_release(st->xs_root);
    sz_free(st);
    return pure_drop(acc);
  }
  void *h = sz_list_head(xs);
  st->xs = sz_list_tail(xs);
  SzIo *io = st->f(h, st->fenv);
  return fm_drop(io, after_map_one, st);
}

static SzIo *after_map_inner(void *inner_acc, void *env) {
  StMap *st = (StMap *)env;
  SzList *xs = reverse_take((SzList *)inner_acc);
  st->xs_root = xs;
  return fold_evalmap(xs, st);
}

static SzIo *fold_evaltap(SzList *xs, StMap *st);

static SzIo *after_tap_one(void *ignored, void *env) {
  StMap *st = (StMap *)env;
  void *h = st->cur;
  st->cur = NULL;
  sz_release(ignored);
  st->outer_acc = cons_take(h, st->outer_acc);
  sz_release(h);
  return fold_evaltap(st->xs, st);
}

static SzIo *fold_evaltap(SzList *xs, StMap *st) {
  if (sz_list_is_empty(xs)) {
    SzList *acc = st->outer_acc;
    sz_release(st->xs_root);
    sz_free(st);
    return pure_drop(acc);
  }
  void *h = sz_list_head(xs);
  st->xs = sz_list_tail(xs);
  sz_retain(h);
  st->cur = h;
  SzIo *io = st->f(h, st->fenv);
  return fm_drop(io, after_tap_one, st);
}

static SzIo *after_tap_inner(void *inner_acc, void *env) {
  StMap *st = (StMap *)env;
  SzList *xs = reverse_take((SzList *)inner_acc);
  st->xs_root = xs;
  st->cur = NULL;
  return fold_evaltap(xs, st);
}

static SzIo *after_sync_map(void *inner_acc, void *env) {
  StSyncMap *st = (StSyncMap *)env;
  SzList *xs = reverse_take((SzList *)inner_acc);
  SzList *root = xs;
  SzList *acc = st->outer_acc;
  while (!sz_list_is_empty(xs)) {
    void *h = sz_list_head(xs);
    void *mapped;
    xs = sz_list_tail(xs);
    /* Mapper returns +1. Cons retains. Drop the mapper ref. */
    mapped = st->f(h, st->fenv);
    acc = cons_take(mapped, acc);
    sz_release(mapped);
  }
  sz_release(root);
  sz_free(st);
  return pure_drop(acc);
}

static SzList *oldest_added(SzList *acc, int64_t acc_len, SzList **old_acc) {
  int64_t added = (int64_t)sz_list_len(acc) - acc_len;
  int64_t i;
  SzList *p = acc;
  SzList *oldest_first = sz_list_nil();
  if (added < 0)
    added = 0;
  for (i = 0; i < added; i++) {
    oldest_first = cons_take(sz_list_head(p), oldest_first);
    p = sz_list_tail(p);
  }
  *old_acc = p;
  return oldest_first;
}

/* acc is newest-first. Keep a prefix of the added segment while pred holds. */
static SzList *takewhile_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                               void *penv, int64_t remain, int *stopped) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  SzList *oldest_root = oldest_first;
  SzList *kept_nf = sz_list_nil();
  SzList *kept_of;
  SzList *kept_of_root;
  SzList *out;
  int64_t n;
  int64_t nkeep;
  int64_t i;
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) == 0) {
      if (stopped)
        *stopped = 1;
      break;
    }
    kept_nf = cons_take(h, kept_nf);
  }
  sz_release(oldest_root);
  kept_of = reverse_take(kept_nf);
  n = (int64_t)sz_list_len(kept_of);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  if (nkeep < 0)
    nkeep = 0;
  sz_retain(old_acc);
  sz_release(acc);
  out = old_acc;
  kept_of_root = kept_of;
  for (i = 0; i < nkeep; i++) {
    out = cons_take(sz_list_head(kept_of), out);
    kept_of = sz_list_tail(kept_of);
  }
  sz_release(kept_of_root);
  return out;
}

/* acc is newest-first. Skip a prefix of the added segment while pred holds. */
static SzList *dropwhile_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                               void *penv, int64_t remain) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  SzList *oldest_root = oldest_first;
  SzList *out;
  SzList *p;
  int64_t n;
  int64_t nkeep;
  int64_t i;
  while (!sz_list_is_empty(oldest_first) &&
         pred(sz_list_head(oldest_first), penv) != 0)
    oldest_first = sz_list_tail(oldest_first);
  n = (int64_t)sz_list_len(oldest_first);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  if (nkeep < 0)
    nkeep = 0;
  sz_retain(old_acc);
  sz_release(acc);
  out = old_acc;
  p = oldest_first;
  for (i = 0; i < nkeep; i++) {
    out = cons_take(sz_list_head(p), out);
    p = sz_list_tail(p);
  }
  sz_release(oldest_root);
  return out;
}

/* acc is newest-first. Skip added items until the first match; keep only that. */
static SzList *find_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                          void *penv, int64_t remain, int *found) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  SzList *oldest_root = oldest_first;
  SzList *out;
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) != 0) {
      if (found)
        *found = 1;
      sz_retain(old_acc);
      sz_release(acc);
      if (remain == 0) {
        sz_release(oldest_root);
        return old_acc;
      }
      out = cons_take(h, old_acc);
      sz_release(oldest_root);
      return out;
    }
  }
  sz_release(oldest_root);
  sz_retain(old_acc);
  sz_release(acc);
  return old_acc;
}

static SzIo *tw_done(void *acc, void *env) {
  sz_free(env);
  return pure_drop(acc);
}

static SzIo *after_tw_eval(void *value, void *env) {
  StTWEval *st = (StTWEval *)env;
  SzList *acc = st->acc;
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int *stopped = st->stopped;
  sz_free(st);
  if (pred(value, penv) == 0) {
    if (stopped)
      *stopped = 1;
    sz_release(value);
    return pure_drop(acc);
  }
  acc = cons_take(value, acc);
  sz_release(value);
  return takewhile_into(tail, acc, remain_dec(remain), pred, penv, stopped);
}

static SzIo *after_tw_concat(void *acc, void *env) {
  StTWConcat *st = (StTWConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int *stopped = st->stopped;
  int64_t added;
  sz_free(st);
  if (stopped && *stopped)
    return pure_drop(acc);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return takewhile_into(right, (SzList *)acc, remain, pred, penv, stopped);
}

static SzIo *after_tw_cut(void *acc, void *env) {
  StTWCut *st = (StTWCut *)env;
  SzList *out = takewhile_added((SzList *)acc, st->acc_len, st->pred, st->penv,
                                st->remain, st->stopped);
  sz_free(st);
  return pure_drop(out);
}

static SzIo *after_dropwhile(void *acc, void *env) {
  StDropWhile *st = (StDropWhile *)env;
  SzList *out =
      dropwhile_added((SzList *)acc, st->acc_len, st->pred, st->penv, st->remain);
  sz_free(st);
  return pure_drop(out);
}

static SzIo *after_filter_eval(void *value, void *env) {
  StTWEval *st = (StTWEval *)env;
  SzList *acc = st->acc;
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  sz_free(st);
  if (pred(value, penv) != 0) {
    acc = cons_take(value, acc);
    sz_release(value);
    return filter_into(tail, acc, remain_dec(remain), pred, penv);
  }
  sz_release(value);
  return filter_into(tail, acc, remain, pred, penv);
}

static SzIo *after_filter_concat(void *acc, void *env) {
  StTWConcat *st = (StTWConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int64_t added;
  sz_free(st);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return filter_into(right, (SzList *)acc, remain, pred, penv);
}

static SzIo *filter_into(SzStream *s, SzList *acc, int64_t remain,
                         SzStreamPred pred, void *penv) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (pred(s->left, penv) != 0) {
        acc = cons_take(s->left, acc);
        remain = remain_dec(remain);
      }
      s = (SzStream *)s->right;
      break;
    case SZ_ST_EVAL: {
      StTWEval *st = (StTWEval *)sz_alloc(sizeof(StTWEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain;
      st->pred = pred;
      st->penv = penv;
      st->stopped = NULL;
      return fm_drop((SzIo *)s->left, after_filter_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = NULL;
      return fm_drop(filter_into((SzStream *)s->left, acc, remain, pred, penv),
                           after_filter_concat, st);
    }
    default: {
      /* Nested map and other tags still compile then cut. */
      StFilter *st = (StFilter *)sz_alloc(sizeof(StFilter));
      st->pred = pred;
      st->penv = penv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return fm_drop(compile_into(s, acc, remain), after_filter, st);
    }
    }
  }
  return pure_drop(acc);
}

static SzIo *after_dw_eval(void *value, void *env) {
  StTWEval *st = (StTWEval *)env;
  SzList *acc = st->acc;
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  sz_free(st);
  if (pred(value, penv) != 0) {
    sz_release(value);
    return dropwhile_into(tail, acc, remain, pred, penv);
  }
  acc = cons_take(value, acc);
  sz_release(value);
  return compile_into(tail, acc, remain_dec(remain));
}

static SzIo *after_dw_concat(void *acc, void *env) {
  StTWConcat *st = (StTWConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int64_t added;
  sz_free(st);
  if ((int64_t)sz_list_len((SzList *)acc) == acc_len)
    return dropwhile_into(right, (SzList *)acc, remain, pred, penv);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return compile_into(right, (SzList *)acc, remain);
}

static SzIo *dropwhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (pred(s->left, penv) != 0) {
        s = (SzStream *)s->right;
        break;
      }
      acc = cons_take(s->left, acc);
      return compile_into((SzStream *)s->right, acc, remain_dec(remain));
    case SZ_ST_EVAL: {
      StTWEval *st = (StTWEval *)sz_alloc(sizeof(StTWEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain;
      st->pred = pred;
      st->penv = penv;
      st->stopped = NULL;
      return fm_drop((SzIo *)s->left, after_dw_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = NULL;
      return fm_drop(
          dropwhile_into((SzStream *)s->left, acc, remain, pred, penv),
          after_dw_concat, st);
    }
    default: {
      StDropWhile *st = (StDropWhile *)sz_alloc(sizeof(StDropWhile));
      st->pred = pred;
      st->penv = penv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return fm_drop(compile_into(s, acc, -1), after_dropwhile, st);
    }
    }
  }
  return pure_drop(acc);
}

static SzIo *compile_into(SzStream *s, SzList *acc, int64_t remain) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_DROP: {
      int64_t n = (int64_t)(intptr_t)s->env;
      int64_t inner_remain;
      StDrop *st;
      if (n <= 0) {
        s = (SzStream *)s->left;
        break;
      }
      inner_remain = remain < 0 ? remain : remain + n;
      st = (StDrop *)sz_alloc(sizeof(StDrop));
      st->n = n;
      st->acc_len = (int64_t)sz_list_len(acc);
      return fm_drop(compile_into((SzStream *)s->left, acc, inner_remain),
                           after_drop, st);
    }
    case SZ_ST_CONS:
      acc = cons_take(s->left, acc);
      s = (SzStream *)s->right;
      remain = remain_dec(remain);
      break;
    case SZ_ST_RANGE: {
      int64_t from = (int64_t)(intptr_t)s->env;
      int64_t until = (int64_t)(intptr_t)s->right;
      while (from < until && remain != 0) {
        void *box = sz_box_i64(from);
        acc = cons_take(box, acc);
        sz_release(box);
        from++;
        remain = remain_dec(remain);
      }
      return pure_drop(acc);
    }
    case SZ_ST_ITERATE: {
      SzPair *pack = (SzPair *)s->env;
      int64_t n = pack ? sz_unbox_i64(pack->left) : 0;
      void *fenv = pack ? pack->right : NULL;
      SzStreamMapFn f = (SzStreamMapFn)s->right;
      void *cur;
      int64_t i;
      if (n <= 0)
        return pure_drop(acc);
      sz_retain(s->left);
      cur = s->left;
      for (i = 0; i < n && remain != 0; i++) {
        acc = cons_take(cur, acc);
        remain = remain_dec(remain);
        if (i + 1 < n && remain != 0) {
          void *next = f(cur, fenv);
          sz_release(cur);
          cur = next;
        }
      }
      sz_release(cur);
      return pure_drop(acc);
    }
    case SZ_ST_UNFOLD: {
      SzStreamMapFn f = (SzStreamMapFn)s->right;
      void *state;
      int64_t steps = 0;
      sz_retain(s->left);
      state = s->left;
      while (remain != 0) {
        SzList *step = (SzList *)f(state, s->env);
        SzPair *p;
        void *a;
        void *next;
        if (sz_list_is_empty(step)) {
          sz_release(step);
          break;
        }
        p = (SzPair *)sz_list_head(step);
        a = sz_pair_left(p);
        next = sz_pair_right(p);
        sz_retain(a);
        sz_retain(next);
        sz_release(step);
        acc = cons_take(a, acc);
        sz_release(a);
        sz_release(state);
        state = next;
        remain = remain_dec(remain);
        steps += 1;
        if (steps >= SZ_UNFOLD_CAP)
          sz_panic("Stream.unfold: did not terminate");
      }
      sz_release(state);
      return pure_drop(acc);
    }
    case SZ_ST_EVAL: {
      StEval *st = (StEval *)sz_alloc(sizeof(StEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain_dec(remain);
      return fm_drop((SzIo *)s->left, after_eval, st);
    }
    case SZ_ST_CONCAT: {
      StConcat *st = (StConcat *)sz_alloc(sizeof(StConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return fm_drop(compile_into((SzStream *)s->left, acc, remain),
                           after_concat, st);
    }
    case SZ_ST_EVALMAP: {
      StMap *st = (StMap *)sz_alloc(sizeof(StMap));
      st->f = (SzCont)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      st->xs = NULL;
      st->xs_root = NULL;
      st->cur = NULL;
      return fm_drop(compile_into((SzStream *)s->left, sz_list_nil(), remain),
                           after_map_inner, st);
    }
    case SZ_ST_FILTER:
      return filter_into((SzStream *)s->left, acc, remain,
                         (SzStreamPred)s->right, s->env);
    case SZ_ST_MAP: {
      StSyncMap *st = (StSyncMap *)sz_alloc(sizeof(StSyncMap));
      st->f = (SzStreamMapFn)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      return fm_drop(compile_into((SzStream *)s->left, sz_list_nil(), remain),
                           after_sync_map, st);
    }
    case SZ_ST_TAKEWHILE: {
      StTW *st = (StTW *)sz_alloc(sizeof(StTW));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->stopped = 0;
      return fm_drop(takewhile_into((SzStream *)s->left, acc, remain, st->pred,
                                          st->penv, &st->stopped),
                           tw_done, st);
    }
    case SZ_ST_DROPWHILE:
      return dropwhile_into((SzStream *)s->left, acc, remain,
                            (SzStreamPred)s->right, s->env);
    case SZ_ST_FIND: {
      StTW *st = (StTW *)sz_alloc(sizeof(StTW));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->stopped = 0;
      return fm_drop(find_into((SzStream *)s->left, acc, remain, st->pred,
                                     st->penv, &st->stopped),
                           tw_done, st);
    }
    case SZ_ST_ZIPIDX:
    case SZ_ST_INTERSPERSE:
    case SZ_ST_GROUPED:
    case SZ_ST_SCAN:
    case SZ_ST_SLIDING:
    case SZ_ST_TAKERIGHT:
    case SZ_ST_DROPRIGHT:
    case SZ_ST_FINDLAST:
      return lift_into(s, acc, remain);
    case SZ_ST_FLATTEN:
      return mapconcat_into((SzStream *)s->left, acc, remain, NULL, NULL);
    case SZ_ST_MAPCONCAT:
      return mapconcat_into((SzStream *)s->left, acc, remain,
                            (SzStreamMapFn)s->right, s->env);
    case SZ_ST_CHANGES:
      return changes_into((SzStream *)s->left, acc, remain, NULL, 0);
    case SZ_ST_FLATMAP:
      return flatmap_into((SzStream *)s->left, acc, remain,
                          (SzStreamMapFn)s->right, s->env);
    case SZ_ST_FILTERNOT:
      return filter_into((SzStream *)s->left, acc, remain, filter_not_pred, s);
    case SZ_ST_ZIP:
    case SZ_ST_ZIPWITH:
    case SZ_ST_ZIPALL:
    case SZ_ST_INTERLEAVE:
      return zip_into(s, acc, remain);
    case SZ_ST_ORELSE: {
      StConcat *st = (StConcat *)sz_alloc(sizeof(StConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return fm_drop(compile_into((SzStream *)s->left, acc, remain),
                     after_or_else, st);
    }
    case SZ_ST_EVALTAP: {
      StMap *st = (StMap *)sz_alloc(sizeof(StMap));
      st->f = (SzCont)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      st->xs = NULL;
      st->xs_root = NULL;
      st->cur = NULL;
      return fm_drop(compile_into((SzStream *)s->left, sz_list_nil(), remain),
                     after_tap_inner, st);
    }
    default:
      sz_panic("sz_stream: bad tag");
    }
  }
  return pure_drop(acc);
}

typedef struct StLift {
  SzList *outer;
  int64_t remain;
  int tag;
  void *arg;
  void *env;
} StLift;

static SzList *keep_oldest(SzList *oldest, int64_t remain) {
  SzList *p;
  SzList *acc;
  SzList *out;
  int64_t n;
  if (remain < 0)
    return oldest;
  acc = sz_list_nil();
  p = oldest;
  n = 0;
  while (!sz_list_is_empty(p) && n < remain) {
    acc = cons_take(sz_list_head(p), acc);
    p = sz_list_tail(p);
    n += 1;
  }
  sz_release(oldest);
  out = reverse_take(acc);
  return out;
}

static SzList *cons_oldest_onto(SzList *oldest, SzList *outer) {
  SzList *p = oldest;
  SzList *root = oldest;
  while (!sz_list_is_empty(p)) {
    outer = cons_take(sz_list_head(p), outer);
    p = sz_list_tail(p);
  }
  sz_release(root);
  return outer;
}

static SzList *stream_changes(SzList *oldest) {
  SzList *p = oldest;
  SzList *acc = sz_list_nil();
  void *prev = NULL;
  int have = 0;
  while (!sz_list_is_empty(p)) {
    void *h = sz_list_head(p);
    p = sz_list_tail(p);
    if (!have || !sz_ptr_eq(prev, h)) {
      acc = cons_take(h, acc);
      prev = h;
      have = 1;
    }
  }
  sz_release(oldest);
  return reverse_take(acc);
}

typedef struct StFlat {
  SzList *outer;
  int64_t remain;
  int64_t acc_len;
  SzStreamMapFn f;
  void *fenv;
  SzList *xs;
  SzList *xs_root;
  SzStream *cur;
} StFlat;

static SzIo *fold_flatmap(StFlat *st);

static SzIo *after_flat_one(void *acc, void *env) {
  StFlat *st = (StFlat *)env;
  int64_t added = (int64_t)sz_list_len((SzList *)acc) - st->acc_len;
  st->outer = (SzList *)acc;
  if (st->cur) {
    sz_release(st->cur);
    st->cur = NULL;
  }
  if (st->remain >= 0) {
    st->remain -= added;
    if (st->remain < 0)
      st->remain = 0;
  }
  return fold_flatmap(st);
}

static SzIo *fold_flatmap(StFlat *st) {
  SzStream *inner;
  if (sz_list_is_empty(st->xs) || st->remain == 0) {
    SzList *acc = st->outer;
    sz_release(st->xs_root);
    sz_free(st);
    return pure_drop(acc);
  }
  inner = (SzStream *)st->f(sz_list_head(st->xs), st->fenv);
  st->xs = sz_list_tail(st->xs);
  st->cur = inner;
  st->acc_len = (int64_t)sz_list_len(st->outer);
  return fm_drop(compile_into(inner, st->outer, st->remain), after_flat_one, st);
}

static SzIo *after_lift(void *inner_acc, void *env) {
  StLift *st = (StLift *)env;
  SzList *xs = reverse_take((SzList *)inner_acc);
  SzList *out = xs;
  SzList *outer = st->outer;
  int64_t remain = st->remain;
  int tag = st->tag;
  void *arg = st->arg;
  void *fenv = st->env;
  sz_free(st);
  if (tag == SZ_ST_ZIPIDX) {
    out = sz_list_zip_with_index(xs);
    sz_release(xs);
  } else if (tag == SZ_ST_INTERSPERSE) {
    out = sz_list_intersperse(xs, arg);
    sz_release(xs);
  } else if (tag == SZ_ST_GROUPED) {
    out = sz_list_grouped(xs, (int64_t)(intptr_t)fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_FLATTEN) {
    out = sz_list_flatten(xs);
    sz_release(xs);
  } else if (tag == SZ_ST_CHANGES) {
    out = stream_changes(xs);
  } else if (tag == SZ_ST_SCAN) {
    SzPair *pack = (SzPair *)fenv;
    SzStreamMapFn fn = (SzStreamMapFn)arg;
    void *z = pack ? pack->left : NULL;
    void *penv = pack ? pack->right : NULL;
    out = sz_list_scan_left(xs, z, (SzListMapFn)fn, penv);
    sz_release(xs);
  } else if (tag == SZ_ST_FLATMAP) {
    StFlat *flat = (StFlat *)sz_alloc(sizeof(StFlat));
    flat->outer = outer;
    flat->remain = remain;
    flat->acc_len = 0;
    flat->f = (SzStreamMapFn)arg;
    flat->fenv = fenv;
    flat->xs = xs;
    flat->xs_root = xs;
    flat->cur = NULL;
    return fold_flatmap(flat);
  } else if (tag == SZ_ST_FILTERNOT) {
    out = sz_list_filter_not(xs, (SzListPred)arg, fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_MAPCONCAT) {
    out = sz_list_flat_map(xs, (SzListMapFn)arg, fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_SLIDING) {
    out = sz_list_sliding(xs, (int64_t)(intptr_t)fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_TAKERIGHT) {
    out = sz_list_take_right(xs, (int64_t)(intptr_t)fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_DROPRIGHT) {
    out = sz_list_drop_right(xs, (int64_t)(intptr_t)fenv);
    sz_release(xs);
  } else if (tag == SZ_ST_FINDLAST) {
    out = sz_list_find_last(xs, (SzListPred)arg, fenv);
    sz_release(xs);
  }
  out = keep_oldest(out, remain);
  return pure_drop(cons_oldest_onto(out, outer));
}

static SzIo *lift_into(SzStream *s, SzList *acc, int64_t remain) {
  StLift *st = (StLift *)sz_alloc(sizeof(StLift));
  int64_t inner_remain = remain;
  st->outer = acc;
  st->remain = remain;
  st->tag = s->tag;
  st->arg = s->right;
  st->env = s->env;
  if (s->tag == SZ_ST_GROUPED && remain >= 0) {
    int64_t n = (int64_t)(intptr_t)s->env;
    inner_remain = n <= 0 ? 0 : remain * n;
  }
  if (s->tag == SZ_ST_SLIDING && remain >= 0) {
    int64_t n = (int64_t)(intptr_t)s->env;
    inner_remain = n <= 0 ? 0 : remain + n - 1;
  }
  /* takeRight, dropRight, and findLast need the whole inner stream. */
  if ((s->tag == SZ_ST_TAKERIGHT || s->tag == SZ_ST_DROPRIGHT ||
       s->tag == SZ_ST_FINDLAST) &&
      remain >= 0)
    inner_remain = -1;
  return fm_drop(compile_into((SzStream *)s->left, sz_list_nil(), inner_remain),
                 after_lift, st);
}

static int64_t filter_not_pred(void *v, void *env) {
  SzStream *node = (SzStream *)env;
  SzStreamPred pred = (SzStreamPred)node->right;
  return pred(v, node->env) == 0;
}

typedef struct StMc {
  SzStreamMapFn f;
  void *fenv;
  int64_t remain;
  int64_t acc_len;
  SzStream *next;
  SzList *acc;
} StMc;

static SzList *emit_chunk(SzList *acc, SzList *chunk, int64_t *remain) {
  SzList *p = chunk;
  while (!sz_list_is_empty(p) && *remain != 0) {
    acc = cons_take(sz_list_head(p), acc);
    p = sz_list_tail(p);
    *remain = remain_dec(*remain);
  }
  return acc;
}

static SzList *mc_apply(SzList *acc, void *v, SzStreamMapFn f, void *fenv,
                        int64_t *remain) {
  SzList *chunk;
  if (f) {
    chunk = (SzList *)f(v, fenv);
    acc = emit_chunk(acc, chunk, remain);
    sz_release(chunk);
  } else {
    acc = emit_chunk(acc, (SzList *)v, remain);
  }
  return acc;
}

static SzIo *after_mc_eval(void *value, void *env) {
  StMc *st = (StMc *)env;
  SzList *acc = st->acc;
  SzStream *next = st->next;
  int64_t remain = st->remain;
  SzStreamMapFn f = st->f;
  void *fenv = st->fenv;
  sz_free(st);
  acc = mc_apply(acc, value, f, fenv, &remain);
  sz_release(value);
  if (remain == 0)
    return pure_drop(acc);
  return mapconcat_into(next, acc, remain, f, fenv);
}

static SzIo *after_mc_concat(void *acc, void *env) {
  StMc *st = (StMc *)env;
  SzStream *right = st->next;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamMapFn f = st->f;
  void *fenv = st->fenv;
  int64_t added;
  sz_free(st);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return mapconcat_into(right, (SzList *)acc, remain, f, fenv);
}

static SzIo *mapconcat_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamMapFn f, void *fenv) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      acc = mc_apply(acc, s->left, f, fenv, &remain);
      s = (SzStream *)s->right;
      break;
    case SZ_ST_EVAL: {
      StMc *st = (StMc *)sz_alloc(sizeof(StMc));
      st->f = f;
      st->fenv = fenv;
      st->remain = remain;
      st->acc_len = 0;
      st->next = (SzStream *)s->right;
      st->acc = acc;
      return fm_drop((SzIo *)s->left, after_mc_eval, st);
    }
    case SZ_ST_CONCAT: {
      StMc *st = (StMc *)sz_alloc(sizeof(StMc));
      st->f = f;
      st->fenv = fenv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->next = (SzStream *)s->right;
      st->acc = acc;
      return fm_drop(mapconcat_into((SzStream *)s->left, acc, remain, f, fenv),
                     after_mc_concat, st);
    }
    default: {
      StLift *st = (StLift *)sz_alloc(sizeof(StLift));
      st->outer = acc;
      st->remain = remain;
      st->tag = f ? SZ_ST_MAPCONCAT : SZ_ST_FLATTEN;
      st->arg = (void *)f;
      st->env = fenv;
      return fm_drop(compile_into(s, sz_list_nil(), -1), after_lift, st);
    }
    }
  }
  return pure_drop(acc);
}

typedef struct StCh {
  void *prev;
  int have;
  int64_t remain;
  int64_t acc_len;
  SzStream *next;
  SzList *acc;
} StCh;

static SzIo *after_ch_eval(void *value, void *env) {
  StCh *st = (StCh *)env;
  SzList *acc = st->acc;
  SzStream *next = st->next;
  int64_t remain = st->remain;
  void *prev = st->prev;
  int have = st->have;
  sz_free(st);
  if (!have || !sz_ptr_eq(prev, value)) {
    acc = cons_take(value, acc);
    prev = value;
    have = 1;
    remain = remain_dec(remain);
  }
  sz_release(value);
  if (remain == 0)
    return pure_drop(acc);
  return changes_into(next, acc, remain, prev, have);
}

static SzIo *after_ch_concat(void *acc, void *env) {
  StCh *st = (StCh *)env;
  SzStream *right = st->next;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  void *prev = st->prev;
  int have = st->have;
  int64_t added;
  sz_free(st);
  added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
  if (added > 0) {
    prev = sz_list_head((SzList *)acc);
    have = 1;
  }
  if (remain >= 0) {
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return changes_into(right, (SzList *)acc, remain, prev, have);
}

static SzIo *changes_into(SzStream *s, SzList *acc, int64_t remain, void *prev,
                          int have) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (!have || !sz_ptr_eq(prev, s->left)) {
        acc = cons_take(s->left, acc);
        prev = s->left;
        have = 1;
        remain = remain_dec(remain);
      }
      s = (SzStream *)s->right;
      break;
    case SZ_ST_EVAL: {
      StCh *st = (StCh *)sz_alloc(sizeof(StCh));
      st->prev = prev;
      st->have = have;
      st->remain = remain;
      st->acc_len = 0;
      st->next = (SzStream *)s->right;
      st->acc = acc;
      return fm_drop((SzIo *)s->left, after_ch_eval, st);
    }
    case SZ_ST_CONCAT: {
      StCh *st = (StCh *)sz_alloc(sizeof(StCh));
      st->prev = prev;
      st->have = have;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->next = (SzStream *)s->right;
      st->acc = acc;
      return fm_drop(changes_into((SzStream *)s->left, acc, remain, prev, have),
                     after_ch_concat, st);
    }
    default: {
      StLift *st = (StLift *)sz_alloc(sizeof(StLift));
      st->outer = acc;
      st->remain = remain;
      st->tag = SZ_ST_CHANGES;
      st->arg = NULL;
      st->env = NULL;
      return fm_drop(compile_into(s, sz_list_nil(), -1), after_lift, st);
    }
    }
  }
  return pure_drop(acc);
}

typedef struct StFp {
  SzStreamMapFn f;
  void *fenv;
  int64_t remain;
  int64_t acc_len;
  SzStream *next;
  SzStream *cur;
  SzList *acc;
} StFp;

static SzIo *after_fp_inner(void *acc, void *env) {
  StFp *st = (StFp *)env;
  int64_t added = (int64_t)sz_list_len((SzList *)acc) - st->acc_len;
  SzStream *next = st->next;
  SzStreamMapFn f = st->f;
  void *fenv = st->fenv;
  int64_t remain = st->remain;
  if (st->cur) {
    sz_release(st->cur);
    st->cur = NULL;
  }
  sz_free(st);
  if (remain >= 0) {
    remain -= added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return flatmap_into(next, (SzList *)acc, remain, f, fenv);
}

static SzIo *flatmap_one(void *v, SzList *acc, int64_t remain, SzStream *next,
                         SzStreamMapFn f, void *fenv, int own_v) {
  StFp *st = (StFp *)sz_alloc(sizeof(StFp));
  SzStream *inner = (SzStream *)f(v, fenv);
  if (own_v)
    sz_release(v);
  st->f = f;
  st->fenv = fenv;
  st->remain = remain;
  st->acc_len = (int64_t)sz_list_len(acc);
  st->next = next;
  st->cur = inner;
  st->acc = acc;
  return fm_drop(compile_into(inner, acc, remain), after_fp_inner, st);
}

static SzIo *after_fp_eval(void *value, void *env) {
  StFp *st = (StFp *)env;
  SzList *acc = st->acc;
  SzStream *next = st->next;
  int64_t remain = st->remain;
  SzStreamMapFn f = st->f;
  void *fenv = st->fenv;
  sz_free(st);
  return flatmap_one(value, acc, remain, next, f, fenv, 1);
}

static SzIo *after_fp_concat(void *acc, void *env) {
  StFp *st = (StFp *)env;
  SzStream *right = st->next;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamMapFn f = st->f;
  void *fenv = st->fenv;
  int64_t added;
  sz_free(st);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return flatmap_into(right, (SzList *)acc, remain, f, fenv);
}

static SzIo *flatmap_into(SzStream *s, SzList *acc, int64_t remain,
                          SzStreamMapFn f, void *fenv) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      return flatmap_one(s->left, acc, remain, (SzStream *)s->right, f, fenv, 0);
    case SZ_ST_EVAL: {
      StFp *st = (StFp *)sz_alloc(sizeof(StFp));
      st->f = f;
      st->fenv = fenv;
      st->remain = remain;
      st->acc_len = 0;
      st->next = (SzStream *)s->right;
      st->cur = NULL;
      st->acc = acc;
      return fm_drop((SzIo *)s->left, after_fp_eval, st);
    }
    case SZ_ST_CONCAT: {
      StFp *st = (StFp *)sz_alloc(sizeof(StFp));
      st->f = f;
      st->fenv = fenv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->next = (SzStream *)s->right;
      st->cur = NULL;
      st->acc = acc;
      return fm_drop(flatmap_into((SzStream *)s->left, acc, remain, f, fenv),
                     after_fp_concat, st);
    }
    default: {
      StLift *st = (StLift *)sz_alloc(sizeof(StLift));
      st->outer = acc;
      st->remain = remain;
      st->tag = SZ_ST_FLATMAP;
      st->arg = (void *)f;
      st->env = fenv;
      return fm_drop(compile_into(s, sz_list_nil(), -1), after_lift, st);
    }
    }
  }
  return pure_drop(acc);
}

typedef struct StZip {
  SzStream *right;
  SzList *outer;
  int64_t remain;
  SzList *left_xs;
  int tag;
  void *extra;
} StZip;

static SzIo *after_zip_right(void *right_acc, void *env) {
  StZip *st = (StZip *)env;
  SzList *left = st->left_xs;
  SzList *right = reverse_take((SzList *)right_acc);
  SzList *zipped;
  SzList *outer = st->outer;
  int64_t remain = st->remain;
  int tag = st->tag;
  void *extra = st->extra;
  sz_free(st);
  if (tag == SZ_ST_ZIPALL) {
    SzPair *pads = (SzPair *)extra;
    zipped = sz_list_zip_all(left, right, pads ? pads->left : NULL,
                             pads ? pads->right : NULL);
  } else if (tag == SZ_ST_INTERLEAVE) {
    zipped = sz_list_interleave(left, right);
  } else {
    zipped = sz_list_zip(left, right);
  }
  sz_release(left);
  sz_release(right);
  if (tag == SZ_ST_ZIPWITH) {
    SzPair *pack = (SzPair *)extra;
    SzStreamMapFn fn = pack ? (SzStreamMapFn)pack->left : NULL;
    void *fenv = pack ? pack->right : NULL;
    SzList *mapped = sz_list_map(zipped, (SzListMapFn)fn, fenv);
    sz_release(zipped);
    zipped = mapped;
  }
  zipped = keep_oldest(zipped, remain);
  return pure_drop(cons_oldest_onto(zipped, outer));
}

static SzIo *after_zip_left(void *left_acc, void *env) {
  StZip *st = (StZip *)env;
  int64_t inner_remain = st->remain;
  st->left_xs = reverse_take((SzList *)left_acc);
  if (st->tag == SZ_ST_INTERLEAVE)
    inner_remain = -1;
  return fm_drop(compile_into(st->right, sz_list_nil(), inner_remain),
                 after_zip_right, st);
}

static SzIo *zip_into(SzStream *s, SzList *acc, int64_t remain) {
  StZip *st = (StZip *)sz_alloc(sizeof(StZip));
  st->right = (SzStream *)s->right;
  st->outer = acc;
  st->remain = remain;
  st->left_xs = NULL;
  st->tag = s->tag;
  st->extra = s->env;
  {
    int64_t inner_remain = remain;
    /* Interleave compiles both sides fully. take does not stop a side early. */
    if (s->tag == SZ_ST_INTERLEAVE)
      inner_remain = -1;
    return fm_drop(compile_into((SzStream *)s->left, sz_list_nil(), inner_remain),
                   after_zip_left, st);
  }
}

static SzIo *takewhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv, int *stopped) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (pred(s->left, penv) == 0) {
        if (stopped)
          *stopped = 1;
        return pure_drop(acc);
      }
      acc = cons_take(s->left, acc);
      s = (SzStream *)s->right;
      remain = remain_dec(remain);
      break;
    case SZ_ST_EVAL: {
      StTWEval *st = (StTWEval *)sz_alloc(sizeof(StTWEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain;
      st->pred = pred;
      st->penv = penv;
      st->stopped = stopped;
      return fm_drop((SzIo *)s->left, after_tw_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = stopped;
      return fm_drop(
          takewhile_into((SzStream *)s->left, acc, remain, pred, penv, stopped),
          after_tw_concat, st);
    }
    default: {
      StTWCut *st = (StTWCut *)sz_alloc(sizeof(StTWCut));
      st->pred = pred;
      st->penv = penv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->stopped = stopped;
      return fm_drop(compile_into(s, acc, remain), after_tw_cut, st);
    }
    }
  }
  return pure_drop(acc);
}

static SzIo *after_find_eval(void *value, void *env) {
  StTWEval *st = (StTWEval *)env;
  SzList *acc = st->acc;
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int *found = st->stopped;
  sz_free(st);
  if (pred(value, penv) != 0) {
    if (found)
      *found = 1;
    if (remain == 0) {
      sz_release(value);
      return pure_drop(acc);
    }
    {
      SzList *out = cons_take(value, acc);
      sz_release(value);
      return pure_drop(out);
    }
  }
  sz_release(value);
  return find_into(tail, acc, remain, pred, penv, found);
}

static SzIo *after_find_concat(void *acc, void *env) {
  StTWConcat *st = (StTWConcat *)env;
  SzStream *right = st->right;
  int64_t remain = st->remain;
  int64_t acc_len = st->acc_len;
  SzStreamPred pred = st->pred;
  void *penv = st->penv;
  int *found = st->stopped;
  int64_t added;
  sz_free(st);
  if (found && *found)
    return pure_drop(acc);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return pure_drop(acc);
  return find_into(right, (SzList *)acc, remain, pred, penv, found);
}

static SzIo *after_find_cut(void *acc, void *env) {
  StTWCut *st = (StTWCut *)env;
  SzList *out = find_added((SzList *)acc, st->acc_len, st->pred, st->penv,
                           st->remain, st->stopped);
  sz_free(st);
  return pure_drop(out);
}

static SzIo *find_into(SzStream *s, SzList *acc, int64_t remain,
                       SzStreamPred pred, void *penv, int *found) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return pure_drop(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return pure_drop(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (pred(s->left, penv) != 0) {
        if (found)
          *found = 1;
        if (remain == 0)
          return pure_drop(acc);
        return pure_drop(cons_take(s->left, acc));
      }
      s = (SzStream *)s->right;
      break;
    case SZ_ST_EVAL: {
      StTWEval *st = (StTWEval *)sz_alloc(sizeof(StTWEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain;
      st->pred = pred;
      st->penv = penv;
      st->stopped = found;
      return fm_drop((SzIo *)s->left, after_find_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = found;
      return fm_drop(
          find_into((SzStream *)s->left, acc, remain, pred, penv, found),
          after_find_concat, st);
    }
    default: {
      StTWCut *st = (StTWCut *)sz_alloc(sizeof(StTWCut));
      st->pred = pred;
      st->penv = penv;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->stopped = found;
      return fm_drop(compile_into(s, acc, remain), after_find_cut, st);
    }
    }
  }
  return pure_drop(acc);
}

static SzIo *reverse_acc(void *acc, void *env) {
  (void)env;
  return pure_drop(reverse_take((SzList *)acc));
}

static void *st_release_io(void *env) {
  /* Delay env pins the stream (RC). The thunk stays empty. Last release
   * is delay_env_drop on that env. */
  (void)env;
  return NULL;
}

SzIo *sz_stream_compile_to_list(SzStream *s) {
  SzIo *body;
  if (!s)
    s = sz_stream_nil();
  else
    sz_retain(s);
  body = fm_drop(compile_into(s, sz_list_nil(), -1), reverse_acc, NULL);
  {
    SzIo *fin = sz_io_delay(st_release_io, s);
    SzIo *ens = sz_io_ensure(body, fin);
    sz_release(body);
    sz_release(fin);
    sz_release(s);
    return ens;
  }
}

static SzIo *drain_discard(void *list, void *env) {
  (void)env;
  sz_release(list);
  return pure_drop(NULL);
}

SzIo *sz_stream_drain(SzStream *s) {
  return fm_drop(sz_stream_compile_to_list(s), drain_discard, NULL);
}

static SzIo *exists_from_list(void *list, void *env) {
  (void)env;
  int64_t n = (int64_t)sz_list_len((SzList *)list);
  SzIo *io = pure_drop(sz_box_i64(n > 0 ? 1 : 0));
  sz_release(list);
  return io;
}

SzIo *sz_stream_exists(SzStream *s, SzStreamPred pred, void *env) {
  SzStream *found = sz_stream_find(s, pred, env);
  SzIo *io = sz_stream_compile_to_list(found);
  sz_release(found);
  return fm_drop(io, exists_from_list, NULL);
}

static int64_t forall_miss_pred(void *v, void *env) {
  StFilter *st = (StFilter *)env;
  return st->pred(v, st->penv) == 0;
}

static SzIo *forall_from_exists(void *box, void *env) {
  int64_t hit = sz_unbox_i64(box);
  sz_release(box);
  sz_free(env);
  return pure_drop(sz_box_i64(hit ? 0 : 1));
}

SzIo *sz_stream_forall(SzStream *s, SzStreamPred pred, void *env) {
  StFilter *st = (StFilter *)sz_alloc(sizeof(StFilter));
  st->pred = pred;
  st->penv = env;
  st->remain = 0;
  st->acc_len = 0;
  return fm_drop(sz_stream_exists(s, forall_miss_pred, st), forall_from_exists,
                 st);
}

static SzIo *fold_from_list(void *list, void *env) {
  StLift *st = (StLift *)env;
  SzList *xs = (SzList *)list;
  SzStreamMapFn fn = (SzStreamMapFn)st->arg;
  void *z = st->env;
  void *out = sz_list_fold_left(xs, z, (SzListMapFn)fn, st->outer);
  sz_release(xs);
  sz_release(z);
  sz_free(st);
  return pure_drop(out);
}

SzIo *sz_stream_fold(SzStream *s, void *z, SzStreamMapFn f, void *env) {
  StLift *st;
  if (!f)
    sz_panic("sz_stream_fold(null fn)");
  st = (StLift *)sz_alloc(sizeof(StLift));
  st->outer = env;
  st->remain = 0;
  st->tag = 0;
  st->arg = (void *)f;
  sz_retain(z);
  st->env = z;
  return fm_drop(sz_stream_compile_to_list(s), fold_from_list, st);
}

static SzIo *head_from_list(void *list, void *env) {
  SzList *xs = (SzList *)list;
  void *h;
  (void)env;
  if (sz_list_is_empty(xs)) {
    sz_release(xs);
    return sz_io_fail_cstr("Stream.head on empty");
  }
  h = sz_list_head(xs);
  sz_retain(h);
  sz_release(xs);
  return pure_drop(h);
}

SzIo *sz_stream_head(SzStream *s) {
  SzStream *one = sz_stream_take(s, 1);
  SzIo *io = sz_stream_compile_to_list(one);
  sz_release(one);
  return fm_drop(io, head_from_list, NULL);
}

static SzIo *last_from_list(void *list, void *env) {
  SzList *xs = (SzList *)list;
  SzList *last;
  void *h;
  (void)env;
  last = sz_list_last(xs);
  sz_release(xs);
  if (sz_list_is_empty(last)) {
    sz_release(last);
    return sz_io_fail_cstr("Stream.last on empty");
  }
  h = sz_list_head(last);
  sz_retain(h);
  sz_release(last);
  return pure_drop(h);
}

SzIo *sz_stream_last(SzStream *s) {
  return fm_drop(sz_stream_compile_to_list(s), last_from_list, NULL);
}

static SzIo *count_from_list(void *list, void *env) {
  SzList *xs = (SzList *)list;
  int64_t n = (int64_t)sz_list_len(xs);
  (void)env;
  sz_release(xs);
  return pure_drop(sz_box_i64(n));
}

SzIo *sz_stream_count(SzStream *s) {
  return fm_drop(sz_stream_compile_to_list(s), count_from_list, NULL);
}

static SzIo *none_from_exists(void *box, void *env) {
  int64_t hit = sz_unbox_i64(box);
  (void)env;
  sz_release(box);
  return pure_drop(sz_box_i64(hit ? 0 : 1));
}

SzIo *sz_stream_none(SzStream *s, SzStreamPred pred, void *env) {
  return fm_drop(sz_stream_exists(s, pred, env), none_from_exists, NULL);
}
