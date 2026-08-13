#include "scuzz_rt.h"

enum {
  SZ_ST_NIL = 0,
  SZ_ST_CONS = 1,
  SZ_ST_EVAL = 2,
  SZ_ST_CONCAT = 3,
  SZ_ST_EVALMAP = 4
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

typedef struct StEval {
  SzStream *tail;
  SzList *acc;
} StEval;

typedef struct StConcat {
  SzStream *right;
} StConcat;

typedef struct StMap {
  SzCont f;
  void *fenv;
  SzList *outer_acc;
  SzList *xs;
} StMap;

static SzIo *compile_into(SzStream *s, SzList *acc);

static SzIo *after_eval(void *value, void *env) {
  StEval *st = (StEval *)env;
  SzList *acc = sz_list_cons(value, st->acc);
  SzStream *tail = st->tail;
  sz_free(st);
  return compile_into(tail, acc);
}

static SzIo *after_concat(void *acc, void *env) {
  StConcat *st = (StConcat *)env;
  SzStream *right = st->right;
  sz_free(st);
  return compile_into(right, (SzList *)acc);
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

static SzIo *compile_into(SzStream *s, SzList *acc) {
  while (s && s->tag != SZ_ST_NIL) {
    switch (s->tag) {
    case SZ_ST_CONS:
      acc = sz_list_cons(s->left, acc);
      s = (SzStream *)s->right;
      break;
    case SZ_ST_EVAL: {
      StEval *st = (StEval *)sz_alloc(sizeof(StEval));
      st->tail = (SzStream *)s->right;
      st->acc = acc;
      return sz_io_flatmap((SzIo *)s->left, after_eval, st);
    }
    case SZ_ST_CONCAT: {
      StConcat *st = (StConcat *)sz_alloc(sizeof(StConcat));
      st->right = (SzStream *)s->right;
      return sz_io_flatmap(compile_into((SzStream *)s->left, acc), after_concat,
                           st);
    }
    case SZ_ST_EVALMAP: {
      StMap *st = (StMap *)sz_alloc(sizeof(StMap));
      st->f = (SzCont)s->right;
      st->fenv = s->env;
      st->outer_acc = acc;
      st->xs = NULL;
      return sz_io_flatmap(compile_into((SzStream *)s->left, sz_list_nil()),
                           after_map_inner, st);
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
  return sz_io_flatmap(compile_into(s, sz_list_nil()), reverse_acc, NULL);
}

static SzIo *drain_discard(void *list, void *env) {
  (void)list;
  (void)env;
  return sz_io_pure(NULL);
}

SzIo *sz_stream_drain(SzStream *s) {
  return sz_io_flatmap(sz_stream_compile_to_list(s), drain_discard, NULL);
}
