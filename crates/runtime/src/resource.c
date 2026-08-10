#include "scuzz_rt.h"

typedef struct ResState {
  SzResource *res;
  void *acquired;
} ResState;

static SzIo *res_after_use(void *use_value, void *env) {
  ResState *st = (ResState *)env;
  st->res->release(st->acquired, st->res->env);
  void *v = use_value;
  sz_free(st);
  return sz_io_pure(v);
}

static SzIo *res_on_error(SzError *err, void *env) {
  ResState *st = (ResState *)env;
  st->res->release(st->acquired, st->res->env);
  sz_free(st);
  return sz_io_fail(err);
}

static SzIo *res_after_acquire(void *acquired, void *env) {
  SzResource *res = (SzResource *)env;
  SzIo *use_io = res->use(acquired, res->use_env);
  ResState *st = (ResState *)sz_alloc(sizeof(ResState));
  st->res = res;
  st->acquired = acquired;
  /* Bracket: release on success (flatMap) and on failure (handleErrorWith). */
  SzIo *after = sz_io_flatmap(use_io, res_after_use, st);
  return sz_io_handle_error_with(after, res_on_error, st);
}

static void *res_acquire_thunk(void *env) {
  SzResource *res = (SzResource *)env;
  return res->acquire(res->env);
}

SzResource *sz_resource_make(SzAcquire acquire, SzRelease release, void *env) {
  if (!acquire || !release)
    sz_panic("sz_resource_make(null acquire/release)");
  SzResource *r = (SzResource *)sz_alloc_zero(sizeof(SzResource));
  r->acquire = acquire;
  r->release = release;
  r->env = env;
  return r;
}

SzIo *sz_resource_use(SzResource *res,
                      SzIo *(*use)(void *acquired, void *env),
                      void *use_env) {
  if (!res || !use)
    sz_panic("sz_resource_use(null)");
  res->use = use;
  res->use_env = use_env;
  SzIo *acq = sz_io_delay(res_acquire_thunk, res);
  return sz_io_flatmap(acq, res_after_acquire, res);
}

void sz_resource_free(SzResource *res) { sz_free(res); }
