#include "scuzz_rt.h"

SzDeferred *sz_deferred_make(void) {
  return (SzDeferred *)sz_alloc_zero(sizeof(SzDeferred));
}

void sz_deferred_free(SzDeferred *d) {
  if (!d)
    return;
  if (d->completed && !d->ok && d->error)
    sz_error_free(d->error);
  sz_free(d);
}

static void *deferred_empty_thunk(void *env) {
  (void)env;
  return sz_deferred_make();
}

SzIo *sz_deferred_empty(void) { return sz_io_delay(deferred_empty_thunk, NULL); }

typedef struct DefCompleteEnv {
  SzDeferred *d;
  void *value;
} DefCompleteEnv;

static void *deferred_complete_thunk(void *env) {
  DefCompleteEnv *e = (DefCompleteEnv *)env;
  if (!e->d->completed) {
    e->d->completed = 1;
    e->d->ok = 1;
    e->d->value = e->value;
  }
  sz_free(e);
  return NULL;
}

SzIo *sz_deferred_complete(SzDeferred *d, void *value) {
  if (!d)
    sz_panic("sz_deferred_complete(null)");
  DefCompleteEnv *e = (DefCompleteEnv *)sz_alloc(sizeof(DefCompleteEnv));
  e->d = d;
  e->value = value;
  return sz_io_delay(deferred_complete_thunk, e);
}

SzIo *sz_deferred_complete_cstr(SzDeferred *d, const char *value) {
  return sz_deferred_complete(d, sz_string_from_cstr(value ? value : ""));
}

typedef struct DefFailEnv {
  SzDeferred *d;
  SzError *err;
} DefFailEnv;

static void *deferred_fail_thunk(void *env) {
  DefFailEnv *e = (DefFailEnv *)env;
  if (!e->d->completed) {
    e->d->completed = 1;
    e->d->ok = 0;
    e->d->error = e->err;
  } else {
    sz_error_free(e->err);
  }
  sz_free(e);
  return NULL;
}

SzIo *sz_deferred_fail(SzDeferred *d, SzError *err) {
  if (!d)
    sz_panic("sz_deferred_fail(null)");
  DefFailEnv *e = (DefFailEnv *)sz_alloc(sizeof(DefFailEnv));
  e->d = d;
  e->err = err ? err : sz_error_new(1, "deferred fail");
  return sz_io_delay(deferred_fail_thunk, e);
}

static SzIo *deferred_get_cont(void *value, void *env) {
  (void)value;
  SzDeferred *d = (SzDeferred *)env;
  if (!d->completed)
    return sz_io_fail_cstr("deferred incomplete");
  if (!d->ok)
    return sz_io_fail(d->error ? sz_error_new(d->error->code,
                                              sz_string_cstr(d->error->message))
                               : sz_error_new(1, "deferred failed"));
  return sz_io_pure(d->value);
}

SzIo *sz_deferred_get(SzDeferred *d) {
  if (!d)
    sz_panic("sz_deferred_get(null)");
  /* delay + cont so it participates in flatMap chains cleanly */
  return sz_io_flatmap(sz_io_pure(NULL), deferred_get_cont, d);
}
