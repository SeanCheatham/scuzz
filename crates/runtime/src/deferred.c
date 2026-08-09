#include "scalui_rt.h"

SuDeferred *su_deferred_make(void) {
  return (SuDeferred *)su_alloc_zero(sizeof(SuDeferred));
}

void su_deferred_free(SuDeferred *d) {
  if (!d)
    return;
  if (d->completed && !d->ok && d->error)
    su_error_free(d->error);
  su_free(d);
}

static void *deferred_empty_thunk(void *env) {
  (void)env;
  return su_deferred_make();
}

SuIo *su_deferred_empty(void) { return su_io_delay(deferred_empty_thunk, NULL); }

typedef struct DefCompleteEnv {
  SuDeferred *d;
  void *value;
} DefCompleteEnv;

static void *deferred_complete_thunk(void *env) {
  DefCompleteEnv *e = (DefCompleteEnv *)env;
  if (!e->d->completed) {
    e->d->completed = 1;
    e->d->ok = 1;
    e->d->value = e->value;
  }
  su_free(e);
  return NULL;
}

SuIo *su_deferred_complete(SuDeferred *d, void *value) {
  if (!d)
    su_panic("su_deferred_complete(null)");
  DefCompleteEnv *e = (DefCompleteEnv *)su_alloc(sizeof(DefCompleteEnv));
  e->d = d;
  e->value = value;
  return su_io_delay(deferred_complete_thunk, e);
}

SuIo *su_deferred_complete_cstr(SuDeferred *d, const char *value) {
  return su_deferred_complete(d, su_string_from_cstr(value ? value : ""));
}

typedef struct DefFailEnv {
  SuDeferred *d;
  SuError *err;
} DefFailEnv;

static void *deferred_fail_thunk(void *env) {
  DefFailEnv *e = (DefFailEnv *)env;
  if (!e->d->completed) {
    e->d->completed = 1;
    e->d->ok = 0;
    e->d->error = e->err;
  } else {
    su_error_free(e->err);
  }
  su_free(e);
  return NULL;
}

SuIo *su_deferred_fail(SuDeferred *d, SuError *err) {
  if (!d)
    su_panic("su_deferred_fail(null)");
  DefFailEnv *e = (DefFailEnv *)su_alloc(sizeof(DefFailEnv));
  e->d = d;
  e->err = err ? err : su_error_new(1, "deferred fail");
  return su_io_delay(deferred_fail_thunk, e);
}

static SuIo *deferred_get_cont(void *value, void *env) {
  (void)value;
  SuDeferred *d = (SuDeferred *)env;
  if (!d->completed)
    return su_io_fail_cstr("deferred incomplete");
  if (!d->ok)
    return su_io_fail(d->error ? su_error_new(d->error->code,
                                              su_string_cstr(d->error->message))
                               : su_error_new(1, "deferred failed"));
  return su_io_pure(d->value);
}

SuIo *su_deferred_get(SuDeferred *d) {
  if (!d)
    su_panic("su_deferred_get(null)");
  /* delay + cont so it participates in flatMap chains cleanly */
  return su_io_flatmap(su_io_pure(NULL), deferred_get_cont, d);
}
