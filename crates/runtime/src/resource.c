#include "scalui_rt.h"

typedef struct ResState {
  SuResource *res;
  void *acquired;
} ResState;

static SuIo *res_after_use(void *use_value, void *env) {
  ResState *st = (ResState *)env;
  st->res->release(st->acquired, st->res->env);
  void *v = use_value;
  su_free(st);
  return su_io_pure(v);
}

static SuIo *res_on_error(SuError *err, void *env) {
  ResState *st = (ResState *)env;
  st->res->release(st->acquired, st->res->env);
  su_free(st);
  return su_io_fail(err);
}

static SuIo *res_after_acquire(void *acquired, void *env) {
  SuResource *res = (SuResource *)env;
  SuIo *use_io = res->use(acquired, res->use_env);
  ResState *st = (ResState *)su_alloc(sizeof(ResState));
  st->res = res;
  st->acquired = acquired;
  /* Bracket: release on success (flatMap) and on failure (handleErrorWith). */
  SuIo *after = su_io_flatmap(use_io, res_after_use, st);
  return su_io_handle_error_with(after, res_on_error, st);
}

static void *res_acquire_thunk(void *env) {
  SuResource *res = (SuResource *)env;
  return res->acquire(res->env);
}

SuResource *su_resource_make(SuAcquire acquire, SuRelease release, void *env) {
  if (!acquire || !release)
    su_panic("su_resource_make(null acquire/release)");
  SuResource *r = (SuResource *)su_alloc_zero(sizeof(SuResource));
  r->acquire = acquire;
  r->release = release;
  r->env = env;
  return r;
}

SuIo *su_resource_use(SuResource *res,
                      SuIo *(*use)(void *acquired, void *env),
                      void *use_env) {
  if (!res || !use)
    su_panic("su_resource_use(null)");
  res->use = use;
  res->use_env = use_env;
  SuIo *acq = su_io_delay(res_acquire_thunk, res);
  return su_io_flatmap(acq, res_after_acquire, res);
}

void su_resource_free(SuResource *res) { su_free(res); }
