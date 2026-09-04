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
  int32_t knd;
  (void)kind;
  if (!m)
    return map_node(k, v, NULL, NULL, sz_map_infer_key_kind(k));
  knd = m->key_kind;
  c = map_cmp(m, k);
  if (c == 0)
    return map_node(k, v, m->left, m->right, knd);
  if (c > 0)
    return map_take_left(m->key, m->val, sz_map_set(m->left, k, v, knd), m->right,
                         knd);
  return map_take_right(m->key, m->val, m->left, sz_map_set(m->right, k, v, knd),
                        knd);
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

void *sz_map_get(SzMap *m, void *k) {
  int c;
  if (!m)
    return sz_adt_new(0, NULL);
  c = map_cmp(m, k);
  if (c == 0)
    return sz_adt_new(1, m->val);
  if (c > 0)
    return sz_map_get(m->left, k);
  return sz_map_get(m->right, k);
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

static SzMap *map_min(SzMap *m) {
  while (m && m->left)
    m = m->left;
  return m;
}

SzMap *sz_map_remove(SzMap *m, void *k) {
  int c;
  SzMap *child;
  SzMap *min;
  if (!m)
    return NULL;
  c = map_cmp(m, k);
  if (c > 0) {
    child = sz_map_remove(m->left, k);
    if (child == m->left) {
      sz_release(child);
      sz_retain(m);
      return m;
    }
    return map_take_left(m->key, m->val, child, m->right, m->key_kind);
  }
  if (c < 0) {
    child = sz_map_remove(m->right, k);
    if (child == m->right) {
      sz_release(child);
      sz_retain(m);
      return m;
    }
    return map_take_right(m->key, m->val, m->left, child, m->key_kind);
  }
  if (!m->left) {
    sz_retain(m->right);
    return m->right;
  }
  if (!m->right) {
    sz_retain(m->left);
    return m->left;
  }
  min = map_min(m->right);
  return map_take_right(min->key, min->val, m->left,
                        sz_map_remove(m->right, min->key), m->key_kind);
}

static SzList *map_field_acc(SzMap *m, SzList *acc, int keys) {
  SzList *next;
  if (!m)
    return acc;
  acc = map_field_acc(m->right, acc, keys);
  next = sz_list_cons(keys ? m->key : m->val, acc);
  sz_release(acc);
  return map_field_acc(m->left, next, keys);
}

SzList *sz_map_keys(SzMap *m) { return map_field_acc(m, NULL, 1); }

SzList *sz_map_values(SzMap *m) { return map_field_acc(m, NULL, 0); }

static SzList *map_pairs_acc(SzMap *m, SzList *acc) {
  SzList *next;
  SzPair *pair;
  if (!m)
    return acc;
  acc = map_pairs_acc(m->right, acc);
  pair = sz_pair_new(m->key, m->val);
  next = sz_list_cons(pair, acc);
  sz_release(pair);
  sz_release(acc);
  return map_pairs_acc(m->left, next);
}

SzList *sz_map_to_list(SzMap *m) { return map_pairs_acc(m, NULL); }

int64_t sz_map_size(SzMap *m) {
  if (!m)
    return 0;
  return 1 + sz_map_size(m->left) + sz_map_size(m->right);
}

/* Overlay `b`'s keys onto `a`. `take_b_val` copies values from `b`; else NULL. */
static SzMap *map_overlay(SzMap *a, SzMap *b, int take_b_val) {
  SzList *keys;
  SzList *p;
  SzMap *cur;
  SzMap *next;
  int32_t kind;
  void *v;
  if (!b) {
    sz_retain(a);
    return a;
  }
  if (!a) {
    sz_retain(b);
    return b;
  }
  kind = a->key_kind;
  cur = a;
  sz_retain(cur);
  keys = sz_map_keys(b);
  for (p = keys; p; p = p->tail) {
    v = take_b_val ? sz_map_get_or(b, p->head, NULL) : NULL;
    next = sz_map_set(cur, p->head, v, kind);
    sz_release(cur);
    cur = next;
  }
  sz_list_free(keys);
  return cur;
}

/* Keep keys of `a` that are (or are not) in `b`. `take_a_val` copies values. */
static SzMap *map_keep(SzMap *a, SzMap *b, int want_in_b, int take_a_val) {
  SzList *keys;
  SzList *p;
  SzMap *out = NULL;
  SzMap *next;
  int32_t kind;
  void *v;
  if (!a)
    return NULL;
  if (!b) {
    if (want_in_b)
      return NULL;
    sz_retain(a);
    return a;
  }
  kind = a->key_kind;
  keys = sz_map_keys(a);
  for (p = keys; p; p = p->tail) {
    if ((sz_map_contains(b, p->head) != 0) == want_in_b) {
      v = take_a_val ? sz_map_get_or(a, p->head, NULL) : NULL;
      next = sz_map_set(out, p->head, v, kind);
      sz_release(out);
      out = next;
    }
  }
  sz_list_free(keys);
  return out;
}

SzMap *sz_set_union(SzMap *a, SzMap *b) { return map_overlay(a, b, 0); }

SzMap *sz_set_intersect(SzMap *a, SzMap *b) { return map_keep(a, b, 1, 0); }

SzMap *sz_set_diff(SzMap *a, SzMap *b) { return map_keep(a, b, 0, 0); }

SzMap *sz_map_union(SzMap *a, SzMap *b) { return map_overlay(a, b, 1); }

SzMap *sz_map_intersect(SzMap *a, SzMap *b) { return map_keep(a, b, 1, 1); }

SzMap *sz_map_diff(SzMap *a, SzMap *b) { return map_keep(a, b, 0, 1); }

/* Rebuild from keys. `on_key` 1 tests the key; else the value. */
static SzMap *map_filter_on(SzMap *m, SzListPred pred, void *env, int on_key,
                            const char *panic) {
  SzList *keys;
  SzList *p;
  SzMap *out = NULL;
  SzMap *next;
  void *v;
  void *arg;
  int32_t kind;
  if (!pred)
    sz_panic(panic);
  if (!m)
    return NULL;
  kind = m->key_kind;
  keys = sz_map_keys(m);
  for (p = keys; p; p = p->tail) {
    v = sz_map_get_or(m, p->head, NULL);
    arg = on_key ? p->head : v;
    if (pred(arg, env) != 0) {
      next = sz_map_set(out, p->head, v, kind);
      sz_release(out);
      out = next;
    }
  }
  sz_list_free(keys);
  return out;
}

static int64_t map_pred_on(SzMap *m, SzListPred pred, void *env, int on_key,
                           int want_all, const char *panic) {
  SzList *keys;
  SzList *p;
  void *v;
  void *arg;
  int64_t ok;
  if (!pred)
    sz_panic(panic);
  if (!m)
    return want_all ? 1 : 0;
  keys = sz_map_keys(m);
  ok = want_all ? 1 : 0;
  for (p = keys; p; p = p->tail) {
    v = sz_map_get_or(m, p->head, NULL);
    arg = on_key ? p->head : v;
    if (want_all) {
      if (pred(arg, env) == 0) {
        ok = 0;
        break;
      }
    } else if (pred(arg, env) != 0) {
      ok = 1;
      break;
    }
  }
  sz_list_free(keys);
  return ok;
}

SzMap *sz_map_filter(SzMap *m, SzListPred pred, void *env) {
  return map_filter_on(m, pred, env, 0, "sz_map_filter(null pred)");
}

SzMap *sz_map_map_values(SzMap *m, SzListMapFn fn, void *env) {
  SzList *keys;
  SzList *p;
  SzMap *out = NULL;
  SzMap *next;
  void *nv;
  int32_t kind;
  if (!fn)
    sz_panic("sz_map_map_values(null fn)");
  if (!m)
    return NULL;
  kind = m->key_kind;
  keys = sz_map_keys(m);
  for (p = keys; p; p = p->tail) {
    /* Mapper returns +1. Set retains. Drop the mapper ref. */
    nv = fn(sz_map_get_or(m, p->head, NULL), env);
    next = sz_map_set(out, p->head, nv, kind);
    sz_release(nv);
    sz_release(out);
    out = next;
  }
  sz_list_free(keys);
  return out;
}

int64_t sz_map_exists(SzMap *m, SzListPred pred, void *env) {
  return map_pred_on(m, pred, env, 0, 0, "sz_map_exists(null pred)");
}

int64_t sz_map_forall(SzMap *m, SzListPred pred, void *env) {
  return map_pred_on(m, pred, env, 0, 1, "sz_map_forall(null pred)");
}

SzMap *sz_set_filter(SzMap *s, SzListPred pred, void *env) {
  return map_filter_on(s, pred, env, 1, "sz_set_filter(null pred)");
}

SzMap *sz_set_map(SzMap *s, SzListMapFn fn, void *env, int32_t key_kind) {
  SzList *keys;
  SzList *p;
  SzMap *out = NULL;
  SzMap *next;
  void *nk;
  int32_t kind = key_kind;
  int got = 0;
  if (!fn)
    sz_panic("sz_set_map(null fn)");
  if (!s)
    return NULL;
  keys = sz_map_keys(s);
  for (p = keys; p; p = p->tail) {
    /* Mapper returns +1. Set retains. Drop the mapper ref. */
    nk = fn(p->head, env);
    if (!got) {
      kind = sz_map_infer_key_kind(nk);
      got = 1;
    }
    next = sz_map_set(out, nk, NULL, kind);
    sz_release(nk);
    sz_release(out);
    out = next;
  }
  sz_list_free(keys);
  return out;
}

int64_t sz_set_exists(SzMap *s, SzListPred pred, void *env) {
  return map_pred_on(s, pred, env, 1, 0, "sz_set_exists(null pred)");
}

int64_t sz_set_forall(SzMap *s, SzListPred pred, void *env) {
  return map_pred_on(s, pred, env, 1, 1, "sz_set_forall(null pred)");
}

static int64_t set_keys_match(SzMap *a, SzMap *b, int want_in_b) {
  SzList *keys;
  SzList *p;
  int64_t ok = 1;
  if (!a)
    return 1;
  keys = sz_map_keys(a);
  for (p = keys; p; p = p->tail) {
    if ((sz_map_contains(b, p->head) != 0) != want_in_b) {
      ok = 0;
      break;
    }
  }
  sz_list_free(keys);
  return ok;
}

int64_t sz_set_is_subset(SzMap *a, SzMap *b) { return set_keys_match(a, b, 1); }

int64_t sz_set_is_disjoint(SzMap *a, SzMap *b) {
  if (!a || !b)
    return 1;
  return set_keys_match(a, b, 0);
}
