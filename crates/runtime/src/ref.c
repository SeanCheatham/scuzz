#include "scuzz_rt.h"

SzRef *sz_ref_make(void *initial) {
  SzRef *r = (SzRef *)sz_rc_alloc(sizeof(SzRef), SZ_RC_REF);
  sz_retain(initial);
  r->value = initial;
  return r;
}

void sz_ref_free(SzRef *r) { sz_release(r); }

static SzRef *ref_make_drop(void *initial) {
  SzRef *r = sz_ref_make(initial);
  sz_release(initial);
  return r;
}

static void *ref_of_thunk(void *env) {
  sz_retain(env);
  return env;
}

SzIo *sz_ref_of(void *initial) {
  SzRef *r = ref_make_drop(initial);
  SzIo *io = sz_io_delay(ref_of_thunk, r);
  sz_release(r);
  return io;
}

SzIo *sz_ref_of_cstr(const char *initial) {
  return sz_ref_of(sz_string_from_cstr(initial ? initial : ""));
}

static void *ref_get_thunk(void *env) {
  SzRef *r = (SzRef *)env;
  void *v = r->value;
  sz_retain(v);
  return v;
}

SzIo *sz_ref_get(SzRef *ref) {
  if (!ref)
    sz_panic("sz_ref_get(null)");
  sz_retain(ref);
  {
    SzIo *io = sz_io_delay(ref_get_thunk, ref);
    sz_release(ref);
    return io;
  }
}

static void *ref_set_thunk(void *env) {
  SzPair *p = (SzPair *)env;
  SzRef *ref = (SzRef *)p->left;
  void *value = p->right;
  void *old = ref->value;
  sz_retain(value);
  ref->value = value;
  sz_release(old);
  return NULL;
}

SzIo *sz_ref_set(SzRef *ref, void *value) {
  if (!ref)
    sz_panic("sz_ref_set(null)");
  {
    SzPair *p = sz_pair_new(ref, value);
    SzIo *io = sz_io_delay(ref_set_thunk, p);
    sz_release(p);
    return io;
  }
}

static SzIo *ref_set_drop(SzRef *ref, void *value) {
  SzIo *io = sz_ref_set(ref, value);
  sz_release(value);
  return io;
}

SzIo *sz_ref_set_cstr(SzRef *ref, const char *value) {
  return ref_set_drop(ref, sz_string_from_cstr(value ? value : ""));
}
