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
    sz_retain(value);
    d->value = value;
    sz_fiber_wake_deferred(d);
  }
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

void sz_deferred_complete_now(SzDeferred *d, void *value) {
  if (!d)
    sz_panic("sz_deferred_complete_now(null)");
  if (d->completed)
    return;
  d->completed = 1;
  d->ok = 1;
  sz_retain(value);
  d->value = value;
  sz_fiber_wake_deferred(d);
  if (d->waiters)
    sz_panic("sz_deferred_complete_now: waiters remain");
}

void sz_deferred_fail_now(SzDeferred *d, SzError *err) {
  if (!d)
    sz_panic("sz_deferred_fail_now(null)");
  if (d->completed)
    return;
  d->completed = 1;
  d->ok = 0;
  if (!err)
    err = sz_error_new(1, "Deferred.fail");
  else
    sz_retain(err);
  d->error = err;
  sz_fiber_wake_deferred(d);
  if (d->waiters)
    sz_panic("sz_deferred_fail_now: waiters remain");
}

static void *deferred_fail_thunk(void *env) {
  SzPair *p = (SzPair *)env;
  sz_deferred_fail_now((SzDeferred *)p->left, (SzError *)p->right);
  return NULL;
}

SzIo *sz_deferred_fail(SzDeferred *d, SzError *err) {
  if (!d)
    sz_panic("sz_deferred_fail(null)");
  {
    SzPair *p = sz_pair_new(d, err);
    SzIo *io = sz_io_delay(deferred_fail_thunk, p);
    sz_release(p);
    return io;
  }
}

SzIo *sz_deferred_get(SzDeferred *d) {
  if (!d)
    sz_panic("sz_deferred_get(null)");
  return sz_io_deferred_get(d);
}
