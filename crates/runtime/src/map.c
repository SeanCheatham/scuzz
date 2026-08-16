#include "scuzz_rt.h"

#include <string.h>

enum { SZ_MAP_INT = 0, SZ_MAP_STR = 1 };

SzMap *sz_map_empty(void) { return NULL; }

static int map_cmp(const SzMap *m, const void *k) {
  if (m->key_kind == SZ_MAP_INT) {
    int64_t a = sz_unbox_i64(m->key);
    int64_t b = sz_unbox_i64(k);
    if (a < b)
      return -1;
    if (a > b)
      return 1;
    return 0;
  }
  return strcmp(sz_string_cstr((const SzString *)m->key),
                sz_string_cstr((const SzString *)k));
}

static SzMap *map_node(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  SzMap *n = (SzMap *)sz_rc_alloc(sizeof(SzMap), SZ_RC_MAP);
  n->key = k;
  n->val = v;
  n->left = l;
  n->right = r;
  n->key_kind = kind;
  sz_retain(k);
  sz_retain(v);
  sz_retain(l);
  sz_retain(r);
  return n;
}

static SzMap *map_take_left(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  SzMap *n = map_node(k, v, l, r, kind);
  sz_release(l);
  return n;
}

static SzMap *map_take_right(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  SzMap *n = map_node(k, v, l, r, kind);
  sz_release(r);
  return n;
}

SzMap *sz_map_set(SzMap *m, void *k, void *v, int32_t kind) {
  int c;
  if (!m)
    return map_node(k, v, NULL, NULL, kind);
  c = map_cmp(m, k);
  if (c == 0)
    return map_node(k, v, m->left, m->right, m->key_kind);
  if (c > 0)
    return map_take_left(m->key, m->val, sz_map_set(m->left, k, v, kind), m->right,
                         m->key_kind);
  return map_take_right(m->key, m->val, m->left, sz_map_set(m->right, k, v, kind),
                        m->key_kind);
}

void *sz_map_get_or(SzMap *m, void *k, void *dflt) {
  int c;
  if (!m)
    return dflt;
  c = map_cmp(m, k);
  if (c == 0)
    return m->val;
  if (c > 0)
    return sz_map_get_or(m->left, k, dflt);
  return sz_map_get_or(m->right, k, dflt);
}

int64_t sz_map_contains(SzMap *m, void *k) {
  int c;
  if (!m)
    return 0;
  c = map_cmp(m, k);
  if (c == 0)
    return 1;
  if (c > 0)
    return sz_map_contains(m->left, k);
  return sz_map_contains(m->right, k);
}
