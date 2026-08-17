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

static void *deferred_complete_thunk(void *env) {
  SzPair *p = (SzPair *)env;
  SzDeferred *d = (SzDeferred *)p->left;
  void *value = p->right;
  if (!d->completed) {
    d->completed = 1;
    d->ok = 1;
    d->value = value;
    p->right = NULL;
    sz_fiber_wake_deferred(d);
  } else {
    sz_release(value);
    p->right = NULL;
  }
  sz_release(p);
  return NULL;
}

SzIo *sz_deferred_complete(SzDeferred *d, void *value) {
  if (!d)
    sz_panic("sz_deferred_complete(null)");
  {
    SzPair *p = sz_pair_new(d, value);
    SzIo *io = sz_io_delay(deferred_complete_thunk, p);
    sz_release(p);
    return io;
  }
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
