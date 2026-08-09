#include "scalui_rt.h"

SuRef *su_ref_make(void *initial) {
  SuRef *r = (SuRef *)su_alloc(sizeof(SuRef));
  r->value = initial;
  return r;
}

void su_ref_free(SuRef *ref) { su_free(ref); }

static void *ref_of_thunk(void *env) { return env; }

SuIo *su_ref_of(void *initial) {
  SuRef *r = su_ref_make(initial);
  return su_io_delay(ref_of_thunk, r);
}

SuIo *su_ref_of_cstr(const char *initial) {
  return su_ref_of(su_string_from_cstr(initial ? initial : ""));
}

static void *ref_get_thunk(void *env) {
  SuRef *r = (SuRef *)env;
  return r->value;
}

SuIo *su_ref_get(SuRef *ref) {
  if (!ref)
    su_panic("su_ref_get(null)");
  return su_io_delay(ref_get_thunk, ref);
}

typedef struct RefSetEnv {
  SuRef *ref;
  void *value;
} RefSetEnv;

static void *ref_set_thunk(void *env) {
  RefSetEnv *e = (RefSetEnv *)env;
  e->ref->value = e->value;
  su_free(e);
  return NULL;
}

SuIo *su_ref_set(SuRef *ref, void *value) {
  if (!ref)
    su_panic("su_ref_set(null)");
  RefSetEnv *e = (RefSetEnv *)su_alloc(sizeof(RefSetEnv));
  e->ref = ref;
  e->value = value;
  return su_io_delay(ref_set_thunk, e);
}

SuIo *su_ref_set_cstr(SuRef *ref, const char *value) {
  return su_ref_set(ref, su_string_from_cstr(value ? value : ""));
}
