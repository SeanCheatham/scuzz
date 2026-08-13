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

typedef struct LangResSt {
  SzLangResource *res;
  SzCont use;
  void *use_env;
  void *acquired;
  void *use_value;
  SzError *use_err;
} LangResSt;

static SzIo *lang_after_release_ok(void *ignored, void *env) {
  (void)ignored;
  LangResSt *st = (LangResSt *)env;
  void *v = st->use_value;
  sz_free(st);
  return sz_io_pure(v);
}

static SzIo *lang_after_release_err(void *ignored, void *env) {
  (void)ignored;
  LangResSt *st = (LangResSt *)env;
  /* Keep st alive: the HANDLE frame below catches this fail and frees st. */
  return sz_io_fail(st->use_err);
}

static SzIo *lang_release_fail_ok_path(SzError *err, void *env) {
  sz_free(env);
  return sz_io_fail(err);
}

static SzIo *lang_release_fail_err_path(SzError *rel_err, void *env) {
  LangResSt *st = (LangResSt *)env;
  SzError *use_err = st->use_err;
  sz_free(st);
  if (rel_err != use_err)
    sz_error_free(rel_err);
  return sz_io_fail(use_err);
}

static SzIo *lang_after_use_ok(void *use_value, void *env) {
  LangResSt *st = (LangResSt *)env;
  st->use_value = use_value;
  SzIo *rel = st->res->release(st->acquired, st->res->release_env);
  SzIo *after = sz_io_flatmap(rel, lang_after_release_ok, st);
  return sz_io_handle_error_with(after, lang_release_fail_ok_path, st);
}

static SzIo *lang_after_use_err(SzError *err, void *env) {
  LangResSt *st = (LangResSt *)env;
  st->use_err = err;
  SzIo *rel = st->res->release(st->acquired, st->res->release_env);
  SzIo *after = sz_io_flatmap(rel, lang_after_release_err, st);
  return sz_io_handle_error_with(after, lang_release_fail_err_path, st);
}

static SzIo *lang_after_acquire(void *acquired, void *env) {
  LangResSt *st = (LangResSt *)env;
  st->acquired = acquired;
  SzIo *use_io = st->use(acquired, st->use_env);
  SzIo *ok = sz_io_flatmap(use_io, lang_after_use_ok, st);
  return sz_io_handle_error_with(ok, lang_after_use_err, st);
}

SzLangResource *sz_lang_resource_make(SzIo *acquire, SzCont release,
                                      void *release_env) {
  if (!acquire || !release)
    sz_panic("sz_lang_resource_make(null acquire/release)");
  SzLangResource *r = (SzLangResource *)sz_alloc_zero(sizeof(SzLangResource));
  r->acquire = acquire;
  r->release = release;
  r->release_env = release_env;
  return r;
}

SzIo *sz_lang_resource_use(SzLangResource *res, SzCont use, void *use_env) {
  if (!res || !use)
    sz_panic("sz_lang_resource_use(null)");
  LangResSt *st = (LangResSt *)sz_alloc_zero(sizeof(LangResSt));
  st->res = res;
  st->use = use;
  st->use_env = use_env;
  return sz_io_flatmap(res->acquire, lang_after_acquire, st);
}

void sz_lang_resource_free(SzLangResource *res) { sz_free(res); }
