#include "scuzz_rt.h"

#include <string.h>

static SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}


static SzIo *pure_drop(void *value) {
  SzIo *io = sz_io_pure(value);
  sz_release(value);
  return io;
}

typedef struct LangResSt {
  SzLangResource *res;
  SzCont use;
  void *use_env;
  void *acquired;
} LangResSt;

static SzIo *lang_fin_free_ok(void *ignored, void *env) {
  LangResSt *st = (LangResSt *)env;
  (void)ignored;
  sz_release(st->use_env);
  st->use_env = NULL;
  sz_release(st->res);
  st->res = NULL;
  sz_free(st);
  return pure_drop(NULL);
}

static SzIo *lang_fin_free_err(SzError *err, void *env) {
  LangResSt *st = (LangResSt *)env;
  sz_release(st->use_env);
  st->use_env = NULL;
  sz_release(st->res);
  st->res = NULL;
  sz_free(st);
  return sz_io_fail(err);
}

static SzIo *lang_after_acquire(void *acquired, void *env) {
  LangResSt *st = (LangResSt *)env;
  st->acquired = acquired;
  SzIo *use_io = st->use(acquired, st->use_env);
  SzIo *rel = st->res->release(acquired, st->res->release_env);
  SzIo *fin = fm_drop(rel, lang_fin_free_ok, st);
  {
    SzIo *handled = sz_io_handle_error_with(fin, lang_fin_free_err, st);
    sz_release(fin);
    fin = handled;
  }
  SzIo *ens = sz_io_ensure(use_io, fin);
  sz_release(use_io);
  sz_release(fin);
  return ens;
}

SzLangResource *sz_lang_resource_make(SzIo *acquire, SzCont release,
                                      void *release_env) {
  if (!acquire || !release)
    sz_panic("sz_lang_resource_make(null acquire/release)");
  SzLangResource *r = (SzLangResource *)sz_rc_alloc(sizeof(SzLangResource),
                                                   SZ_RC_RESOURCE);
  memset(r, 0, sizeof(SzLangResource));
  sz_retain(acquire);
  r->acquire = acquire;
  r->release = release;
  sz_retain(release_env);
  r->release_env = release_env;
  return r;
}

SzIo *sz_lang_resource_use(SzLangResource *res, SzCont use, void *use_env) {
  if (!res || !use)
    sz_panic("sz_lang_resource_use(null)");
  LangResSt *st = (LangResSt *)sz_alloc_zero(sizeof(LangResSt));
  st->res = res;
  st->use = use;
  sz_retain(res);
  sz_retain(use_env);
  st->use_env = use_env;
  sz_retain(res->acquire);
  return fm_drop(res->acquire, lang_after_acquire, st);
}

void sz_lang_resource_free(SzLangResource *res) { sz_release(res); }
