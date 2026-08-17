#include "scuzz_rt.h"

SzDeferred *sz_deferred_make(void) {
  SzDeferred *d = (SzDeferred *)sz_rc_alloc(sizeof(SzDeferred), SZ_RC_DEFERRED);
  d->completed = 0;
  d->ok = 0;
  d->value = NULL;
  d->error = NULL;
  d->waiters = NULL;
  return d;
}

void sz_deferred_free(SzDeferred *d) { sz_release(d); }

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
    sz_fiber_wake_deferred(e->d);
  } else
    sz_release(e->value);
  sz_release(e->d);
  sz_free(e);
  return NULL;
}

SzIo *sz_deferred_complete(SzDeferred *d, void *value) {
  if (!d)
    sz_panic("sz_deferred_complete(null)");
  DefCompleteEnv *e = (DefCompleteEnv *)sz_alloc(sizeof(DefCompleteEnv));
  e->d = d;
  sz_retain(d);
  sz_retain(value);
  e->value = value;
  return sz_io_delay(deferred_complete_thunk, e);
}

static SzIo *deferred_complete_drop(SzDeferred *d, void *value) {
  SzIo *io = sz_deferred_complete(d, value);
  sz_release(value);
  return io;
}

SzIo *sz_deferred_complete_cstr(SzDeferred *d, const char *value) {
  return deferred_complete_drop(d, sz_string_from_cstr(value ? value : ""));
}

SzIo *sz_deferred_get(SzDeferred *d) {
  if (!d)
    sz_panic("sz_deferred_get(null)");
  return sz_io_deferred_get(d);
}
