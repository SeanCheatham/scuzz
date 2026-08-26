#include "scuzz_rt.h"
#include "rt_util.h"

#include <string.h>

typedef struct LangResSt {
  SzLangResource *res;
  SzCont use;
  void *use_env;
  void *acquired;
} LangResSt;

static SzIo *lang_fin_free_ok(void *ignored, void *env) {
  LangResSt *st = (LangResSt *)env;
  sz_release(ignored);
  sz_release(st->acquired);
  st->acquired = NULL;
  sz_release(st->use_env);
  st->use_env = NULL;
  sz_release(st->res);
  st->res = NULL;
  return pure_drop(NULL);
}

static SzIo *lang_fin_free_err(SzError *err, void *env) {
  LangResSt *st = (LangResSt *)env;
  sz_release(st->acquired);
  st->acquired = NULL;
  sz_release(st->use_env);
  st->use_env = NULL;
  sz_release(st->res);
  st->res = NULL;
  return fail_drop(err);
}

static SzIo *use_after_acquire(void *acquired, void *env) {
  SzPair *pack = (SzPair *)env;
  SzPair *inner = (SzPair *)pack->right;
  LangResSt *st = (LangResSt *)sz_rc_alloc(sizeof(LangResSt), SZ_RC_BOX);
  SzIo *use_io;
  SzIo *rel;
  SzIo *fin;
  SzIo *ens;
  memset(st, 0, sizeof(LangResSt));
  sz_retain(pack->left);
  st->res = (SzLangResource *)pack->left;
  st->use = *(SzCont *)inner->right;
  sz_retain(inner->left);
  st->use_env = inner->left;
  st->acquired = acquired;
  use_io = st->use(acquired, st->use_env);
  rel = st->res->release(acquired, st->res->release_env);
  fin = fm_drop(rel, lang_fin_free_ok, st);
  {
    SzIo *handled = sz_io_handle_error_with(fin, lang_fin_free_err, st);
    sz_release(fin);
    fin = handled;
  }
  ens = sz_io_ensure(use_io, fin);
  sz_release(use_io);
  sz_release(fin);
  sz_release(st);
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
  SzCont *use_cell;
  SzPair *inner;
  SzPair *pack;
  if (!res || !use)
    sz_panic("sz_lang_resource_use(null)");
  use_cell = (SzCont *)sz_rc_alloc(sizeof(SzCont), SZ_RC_BOX);
  *use_cell = use;
  inner = sz_pair_new(use_env, use_cell);
  sz_release(use_cell);
  pack = sz_pair_new(res, inner);
  sz_release(inner);
  sz_retain(res->acquire);
  {
    SzIo *io = fm_drop(res->acquire, use_after_acquire, pack);
    sz_release(pack);
    return io;
  }
}

void sz_lang_resource_free(SzLangResource *res) { sz_release(res); }
