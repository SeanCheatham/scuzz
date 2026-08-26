#include "scuzz_rt.h"
#include "rt_util.h"

#include <stdint.h>
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
static SzIo *takewhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv, int *stopped);
static SzIo *find_into(SzStream *s, SzList *acc, int64_t remain,
                       SzStreamPred pred, void *penv, int *found);
static SzIo *filter_into(SzStream *s, SzList *acc, int64_t remain,
                         SzStreamPred pred, void *penv);
static SzIo *dropwhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv);

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
      return fm_drop(compile_into(s, acc, -1), after_filter, st);
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
    default:
      sz_panic("sz_stream: bad tag");
    }
  }
  return pure_drop(acc);
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
