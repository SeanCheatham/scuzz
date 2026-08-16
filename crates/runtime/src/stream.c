#include "scuzz_rt.h"

#include <stdint.h>

enum {
  SZ_ST_NIL = 0,
  SZ_ST_CONS = 1,
  SZ_ST_EVAL = 2,
  SZ_ST_CONCAT = 3,
  SZ_ST_EVALMAP = 4,
  SZ_ST_TAKE = 5,
  SZ_ST_DROP = 6,
  SZ_ST_FILTER = 7,
  SZ_ST_MAP = 8,
  SZ_ST_TAKEWHILE = 9,
  SZ_ST_DROPWHILE = 10,
  SZ_ST_FIND = 11
};

static SzStream *st_new(int tag, void *left, void *right, void *env) {
  SzStream *s = (SzStream *)sz_alloc_zero(sizeof(SzStream));
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
  if (!left)
    left = sz_stream_nil();
  if (!right)
    right = sz_stream_nil();
  return st_new(SZ_ST_CONCAT, left, right, NULL);
}

SzStream *sz_stream_evalmap(SzStream *inner, SzCont f, void *env) {
  if (!inner || !f)
    sz_panic("sz_stream_evalmap(null)");
  return st_new(SZ_ST_EVALMAP, inner, (void *)f, env);
}

SzStream *sz_stream_filter(SzStream *inner, SzStreamPred pred, void *env) {
  if (!inner)
    inner = sz_stream_nil();
  if (!pred)
    sz_panic("sz_stream_filter(null pred)");
  return st_new(SZ_ST_FILTER, inner, (void *)pred, env);
}

SzStream *sz_stream_map(SzStream *inner, SzStreamMapFn f, void *env) {
  if (!inner)
    inner = sz_stream_nil();
  if (!f)
    sz_panic("sz_stream_map(null fn)");
  return st_new(SZ_ST_MAP, inner, (void *)f, env);
}

SzStream *sz_stream_takewhile(SzStream *inner, SzStreamPred pred, void *env) {
  if (!inner)
    inner = sz_stream_nil();
  if (!pred)
    sz_panic("sz_stream_takewhile(null pred)");
  return st_new(SZ_ST_TAKEWHILE, inner, (void *)pred, env);
}

SzStream *sz_stream_dropwhile(SzStream *inner, SzStreamPred pred, void *env) {
  if (!inner)
    inner = sz_stream_nil();
  if (!pred)
    sz_panic("sz_stream_dropwhile(null pred)");
  return st_new(SZ_ST_DROPWHILE, inner, (void *)pred, env);
}

SzStream *sz_stream_find(SzStream *inner, SzStreamPred pred, void *env) {
  if (!inner)
    inner = sz_stream_nil();
  if (!pred)
    sz_panic("sz_stream_find(null pred)");
  return st_new(SZ_ST_FIND, inner, (void *)pred, env);
}

SzStream *sz_stream_take(SzStream *inner, int64_t n) {
  if (!inner)
    inner = sz_stream_nil();
  if (n <= 0)
    return sz_stream_nil();
  return st_new(SZ_ST_TAKE, inner, NULL, (void *)(intptr_t)n);
}

SzStream *sz_stream_drop(SzStream *inner, int64_t n) {
  if (!inner)
    inner = sz_stream_nil();
  if (n <= 0)
    return inner;
  return st_new(SZ_ST_DROP, inner, NULL, (void *)(intptr_t)n);
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

static int64_t remain_dec(int64_t remain) {
  return remain < 0 ? remain : remain - 1;
}

static SzIo *after_eval(void *value, void *env) {
  StEval *st = (StEval *)env;
  SzList *acc = sz_list_cons(value, st->acc);
  SzStream *tail = st->tail;
  int64_t remain = st->remain;
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
  SzList *p = acc;
  if (n < 0)
    n = 0;
  if (added < 0)
    added = 0;
  keep = added - n;
  if (keep < 0)
    keep = 0;
  for (i = 0; i < keep; i++) {
    kept_rev = sz_list_cons(sz_list_head(p), kept_rev);
    p = sz_list_tail(p);
  }
  for (i = 0; i < added - keep; i++)
    p = sz_list_tail(p);
  while (!sz_list_is_empty(kept_rev)) {
    p = sz_list_cons(sz_list_head(kept_rev), p);
    kept_rev = sz_list_tail(kept_rev);
  }
  return p;
}

static SzIo *after_drop(void *acc, void *env) {
  StDrop *st = (StDrop *)env;
  SzList *out = drop_added((SzList *)acc, st->acc_len, st->n);
  sz_free(st);
  return sz_io_pure(out);
}

/* acc is newest-first. Keep matching items from the segment added after acc_len.
   If remain >= 0, keep only the oldest `remain` matches. */
static SzList *filter_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                            void *penv, int64_t remain) {
  int64_t added = (int64_t)sz_list_len(acc) - acc_len;
  int64_t i;
  SzList *p = acc;
  SzList *oldest_first = sz_list_nil();
  SzList *kept_nf = sz_list_nil();
  SzList *kept_of;
  int64_t nkeep;
  int64_t n;
  if (added < 0)
    added = 0;
  for (i = 0; i < added; i++) {
    oldest_first = sz_list_cons(sz_list_head(p), oldest_first);
    p = sz_list_tail(p);
  }
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) != 0)
      kept_nf = sz_list_cons(h, kept_nf);
  }
  kept_of = sz_list_reverse(kept_nf);
  n = (int64_t)sz_list_len(kept_of);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  if (nkeep < 0)
    nkeep = 0;
  for (i = 0; i < nkeep; i++) {
    p = sz_list_cons(sz_list_head(kept_of), p);
    kept_of = sz_list_tail(kept_of);
  }
  return p;
}

static SzIo *after_filter(void *acc, void *env) {
  StFilter *st = (StFilter *)env;
  SzList *out =
      filter_added((SzList *)acc, st->acc_len, st->pred, st->penv, st->remain);
  sz_free(st);
  return sz_io_pure(out);
}

static SzIo *fold_evalmap(SzList *xs, StMap *st);

static SzIo *after_map_one(void *value, void *env) {
  StMap *st = (StMap *)env;
  st->outer_acc = sz_list_cons(value, st->outer_acc);
  return fold_evalmap(st->xs, st);
}

static SzIo *fold_evalmap(SzList *xs, StMap *st) {
  if (sz_list_is_empty(xs)) {
    SzList *acc = st->outer_acc;
    sz_free(st);
    return sz_io_pure(acc);
  }
  void *h = sz_list_head(xs);
  st->xs = sz_list_tail(xs);
  SzIo *io = st->f(h, st->fenv);
  return sz_io_flatmap(io, after_map_one, st);
}

static SzIo *after_map_inner(void *inner_acc, void *env) {
  StMap *st = (StMap *)env;
  SzList *xs = sz_list_reverse((SzList *)inner_acc);
  return fold_evalmap(xs, st);
}

static SzIo *after_sync_map(void *inner_acc, void *env) {
  StSyncMap *st = (StSyncMap *)env;
  SzList *xs = sz_list_reverse((SzList *)inner_acc);
  SzList *acc = st->outer_acc;
  while (!sz_list_is_empty(xs)) {
    void *h = sz_list_head(xs);
    xs = sz_list_tail(xs);
    acc = sz_list_cons(st->f(h, st->fenv), acc);
  }
  sz_free(st);
  return sz_io_pure(acc);
}

static SzList *oldest_added(SzList *acc, int64_t acc_len, SzList **old_acc) {
  int64_t added = (int64_t)sz_list_len(acc) - acc_len;
  int64_t i;
  SzList *p = acc;
  SzList *oldest_first = sz_list_nil();
  if (added < 0)
    added = 0;
  for (i = 0; i < added; i++) {
    oldest_first = sz_list_cons(sz_list_head(p), oldest_first);
    p = sz_list_tail(p);
  }
  *old_acc = p;
  return oldest_first;
}

static SzList *cons_oldest_n(SzList *old_acc, SzList *oldest_first, int64_t nkeep) {
  int64_t i;
  if (nkeep < 0)
    nkeep = 0;
  for (i = 0; i < nkeep; i++) {
    old_acc = sz_list_cons(sz_list_head(oldest_first), old_acc);
    oldest_first = sz_list_tail(oldest_first);
  }
  return old_acc;
}

/* acc is newest-first. Keep a prefix of the added segment while pred holds. */
static SzList *takewhile_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                               void *penv, int64_t remain, int *stopped) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  SzList *kept_nf = sz_list_nil();
  SzList *kept_of;
  int64_t n;
  int64_t nkeep;
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) == 0) {
      if (stopped)
        *stopped = 1;
      break;
    }
    kept_nf = sz_list_cons(h, kept_nf);
  }
  kept_of = sz_list_reverse(kept_nf);
  n = (int64_t)sz_list_len(kept_of);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  return cons_oldest_n(old_acc, kept_of, nkeep);
}

/* acc is newest-first. Skip a prefix of the added segment while pred holds. */
static SzList *dropwhile_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                               void *penv, int64_t remain) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  int64_t n;
  int64_t nkeep;
  while (!sz_list_is_empty(oldest_first) &&
         pred(sz_list_head(oldest_first), penv) != 0)
    oldest_first = sz_list_tail(oldest_first);
  n = (int64_t)sz_list_len(oldest_first);
  nkeep = remain < 0 ? n : remain;
  if (nkeep > n)
    nkeep = n;
  return cons_oldest_n(old_acc, oldest_first, nkeep);
}

/* acc is newest-first. Skip added items until the first match; keep only that. */
static SzList *find_added(SzList *acc, int64_t acc_len, SzStreamPred pred,
                          void *penv, int64_t remain, int *found) {
  SzList *old_acc;
  SzList *oldest_first = oldest_added(acc, acc_len, &old_acc);
  while (!sz_list_is_empty(oldest_first)) {
    void *h = sz_list_head(oldest_first);
    oldest_first = sz_list_tail(oldest_first);
    if (pred(h, penv) != 0) {
      if (found)
        *found = 1;
      if (remain == 0)
        return old_acc;
      return sz_list_cons(h, old_acc);
    }
  }
  return old_acc;
}

static SzIo *tw_done(void *acc, void *env) {
  sz_free(env);
  return sz_io_pure(acc);
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
    return sz_io_pure(acc);
  }
  acc = sz_list_cons(value, acc);
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
    return sz_io_pure(acc);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return sz_io_pure(acc);
  return takewhile_into(right, (SzList *)acc, remain, pred, penv, stopped);
}

static SzIo *after_tw_cut(void *acc, void *env) {
  StTWCut *st = (StTWCut *)env;
  SzList *out = takewhile_added((SzList *)acc, st->acc_len, st->pred, st->penv,
                                st->remain, st->stopped);
  sz_free(st);
  return sz_io_pure(out);
}

static SzIo *after_dropwhile(void *acc, void *env) {
  StDropWhile *st = (StDropWhile *)env;
  SzList *out =
      dropwhile_added((SzList *)acc, st->acc_len, st->pred, st->penv, st->remain);
  sz_free(st);
  return sz_io_pure(out);
}

static SzIo *compile_into(SzStream *s, SzList *acc, int64_t remain) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return sz_io_pure(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return sz_io_pure(acc);
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
      return sz_io_flatmap(compile_into((SzStream *)s->left, acc, inner_remain),
                           after_drop, st);
    }
    case SZ_ST_CONS:
      acc = sz_list_cons(s->left, acc);
      s = (SzStream *)s->right;
      remain = remain_dec(remain);
      break;
    case SZ_ST_EVAL: {
      StEval *st = (StEval *)sz_alloc(sizeof(StEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      st->remain = remain_dec(remain);
      return sz_io_flatmap((SzIo *)s->left, after_eval, st);
    }
    case SZ_ST_CONCAT: {
      StConcat *st = (StConcat *)sz_alloc(sizeof(StConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return sz_io_flatmap(compile_into((SzStream *)s->left, acc, remain),
                           after_concat, st);
    }
    case SZ_ST_EVALMAP: {
      StMap *st = (StMap *)sz_alloc(sizeof(StMap));
      st->f = (SzCont)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      st->xs = NULL;
      return sz_io_flatmap(compile_into((SzStream *)s->left, sz_list_nil(), remain),
                           after_map_inner, st);
    }
    case SZ_ST_FILTER: {
      StFilter *st = (StFilter *)sz_alloc(sizeof(StFilter));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return sz_io_flatmap(compile_into((SzStream *)s->left, acc, -1), after_filter,
                           st);
    }
    case SZ_ST_MAP: {
      StSyncMap *st = (StSyncMap *)sz_alloc(sizeof(StSyncMap));
      st->f = (SzStreamMapFn)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      return sz_io_flatmap(compile_into((SzStream *)s->left, sz_list_nil(), remain),
                           after_sync_map, st);
    }
    case SZ_ST_TAKEWHILE: {
      StTW *st = (StTW *)sz_alloc(sizeof(StTW));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->stopped = 0;
      return sz_io_flatmap(takewhile_into((SzStream *)s->left, acc, remain, st->pred,
                                          st->penv, &st->stopped),
                           tw_done, st);
    }
    case SZ_ST_DROPWHILE: {
      StDropWhile *st = (StDropWhile *)sz_alloc(sizeof(StDropWhile));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      return sz_io_flatmap(compile_into((SzStream *)s->left, acc, -1),
                           after_dropwhile, st);
    }
    case SZ_ST_FIND: {
      StTW *st = (StTW *)sz_alloc(sizeof(StTW));
      st->pred = (SzStreamPred)s->right;
      st->penv = s->env;
      st->stopped = 0;
      return sz_io_flatmap(find_into((SzStream *)s->left, acc, remain, st->pred,
                                     st->penv, &st->stopped),
                           tw_done, st);
    }
    default:
      sz_panic("sz_stream: bad tag");
    }
  }
  return sz_io_pure(acc);
}

static SzIo *takewhile_into(SzStream *s, SzList *acc, int64_t remain,
                            SzStreamPred pred, void *penv, int *stopped) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return sz_io_pure(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return sz_io_pure(acc);
      if (remain < 0 || n < remain)
        remain = n;
      s = (SzStream *)s->left;
      break;
    }
    case SZ_ST_CONS:
      if (pred(s->left, penv) == 0) {
        if (stopped)
          *stopped = 1;
        return sz_io_pure(acc);
      }
      acc = sz_list_cons(s->left, acc);
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
      return sz_io_flatmap((SzIo *)s->left, after_tw_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = stopped;
      return sz_io_flatmap(
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
      return sz_io_flatmap(compile_into(s, acc, remain), after_tw_cut, st);
    }
    }
  }
  return sz_io_pure(acc);
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
    if (remain == 0)
      return sz_io_pure(acc);
    return sz_io_pure(sz_list_cons(value, acc));
  }
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
    return sz_io_pure(acc);
  if (remain >= 0) {
    added = (int64_t)sz_list_len((SzList *)acc) - acc_len;
    remain = remain - added;
    if (remain < 0)
      remain = 0;
  }
  if (remain == 0)
    return sz_io_pure(acc);
  return find_into(right, (SzList *)acc, remain, pred, penv, found);
}

static SzIo *after_find_cut(void *acc, void *env) {
  StTWCut *st = (StTWCut *)env;
  SzList *out = find_added((SzList *)acc, st->acc_len, st->pred, st->penv,
                           st->remain, st->stopped);
  sz_free(st);
  return sz_io_pure(out);
}

static SzIo *find_into(SzStream *s, SzList *acc, int64_t remain,
                       SzStreamPred pred, void *penv, int *found) {
  while (s && s->tag != SZ_ST_NIL) {
    if (remain == 0)
      return sz_io_pure(acc);
    switch (s->tag) {
    case SZ_ST_TAKE: {
      int64_t n = (int64_t)(intptr_t)s->env;
      if (n <= 0)
        return sz_io_pure(acc);
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
          return sz_io_pure(acc);
        return sz_io_pure(sz_list_cons(s->left, acc));
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
      return sz_io_flatmap((SzIo *)s->left, after_find_eval, st);
    }
    case SZ_ST_CONCAT: {
      StTWConcat *st = (StTWConcat *)sz_alloc(sizeof(StTWConcat));
      st->right = (SzStream *)s->right;
      st->remain = remain;
      st->acc_len = (int64_t)sz_list_len(acc);
      st->pred = pred;
      st->penv = penv;
      st->stopped = found;
      return sz_io_flatmap(
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
      return sz_io_flatmap(compile_into(s, acc, remain), after_find_cut, st);
    }
    }
  }
  return sz_io_pure(acc);
}

static SzIo *reverse_acc(void *acc, void *env) {
  (void)env;
  return sz_io_pure(sz_list_reverse((SzList *)acc));
}

SzIo *sz_stream_compile_to_list(SzStream *s) {
  return sz_io_flatmap(compile_into(s, sz_list_nil(), -1), reverse_acc, NULL);
}

static SzIo *drain_discard(void *list, void *env) {
  (void)list;
  (void)env;
  return sz_io_pure(NULL);
}

SzIo *sz_stream_drain(SzStream *s) {
  return sz_io_flatmap(sz_stream_compile_to_list(s), drain_discard, NULL);
}

static SzIo *exists_from_list(void *list, void *env) {
  (void)env;
  int64_t n = (int64_t)sz_list_len((SzList *)list);
  return sz_io_pure(sz_box_i64(n > 0 ? 1 : 0));
}

SzIo *sz_stream_exists(SzStream *s, SzStreamPred pred, void *env) {
  return sz_io_flatmap(sz_stream_compile_to_list(sz_stream_find(s, pred, env)),
                       exists_from_list, NULL);
}
