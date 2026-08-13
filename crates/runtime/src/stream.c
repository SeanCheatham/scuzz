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
  SZ_ST_FILTER = 7
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
  return st_new(SZ_ST_CONS, value, sz_stream_nil(), NULL);
}

SzStream *sz_stream_emits(SzList *xs) {
  SzStream *s = sz_stream_nil();
  SzList *rev = sz_list_reverse(xs);
  while (!sz_list_is_empty(rev)) {
    s = st_new(SZ_ST_CONS, sz_list_head(rev), s, NULL);
    rev = sz_list_tail(rev);
  }
  return s;
}

SzStream *sz_stream_eval(SzIo *io) {
  if (!io)
    sz_panic("sz_stream_eval(null)");
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

/* remain < 0 means unlimited. */
static SzIo *compile_into(SzStream *s, SzList *acc, int64_t remain);

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
    default:
      sz_panic("sz_stream: bad tag");
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
