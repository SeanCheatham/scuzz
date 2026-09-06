#include "scuzz_rt.h"

#include <string.h>

enum { SZ_MAP_INT = 0, SZ_MAP_STR = 1 };

/* Size-balance ratios. Rotate when one child outweighs the other past
 * DELTA. Prefer a double rotation when the heavy child's inner side
 * outweighs its outer side past GAMMA. */
enum { SZ_WBT_DELTA = 3, SZ_WBT_GAMMA = 2 };

SzMap *sz_map_empty(void) { return NULL; }

static int64_t map_len(const SzMap *m) { return m ? m->size : 0; }

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

/* Fresh node (+1). All inputs borrowed. Caches the subtree count. */
static SzMap *map_node(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  SzMap *n = (SzMap *)sz_rc_alloc(sizeof(SzMap), SZ_RC_MAP);
  n->key = k;
  n->val = v;
  n->left = l;
  n->right = r;
  n->key_kind = kind;
  n->size = 1 + map_len(l) + map_len(r);
  sz_retain(k);
  sz_retain(v);
  sz_retain(l);
  sz_retain(r);
  return n;
}

/* Rotations. Inputs borrowed. Result +1. r (or l) is non-null. */
static SzMap *map_rot_left(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  SzMap *new_left = map_node(k, v, l, r->left, kind);
  SzMap *out = map_node(r->key, r->val, new_left, r->right, kind);
  sz_release(new_left);
  return out;
}

static SzMap *map_rot_right(void *k, void *v, SzMap *l, SzMap *r,
                            int32_t kind) {
  SzMap *new_right = map_node(k, v, l->right, r, kind);
  SzMap *out = map_node(l->key, l->val, l->left, new_right, kind);
  sz_release(new_right);
  return out;
}

/* Join borrowed children under (k, v). Rebalance one level. Result +1.
 * Call only when `l` and `r` are balanced and their sizes differ by the
 * work of one insert or remove. */
static SzMap *map_balance(void *k, void *v, SzMap *l, SzMap *r, int32_t kind) {
  int64_t sl = map_len(l);
  int64_t sr = map_len(r);
  if (sl + sr <= 1)
    return map_node(k, v, l, r, kind);
  if (sr > SZ_WBT_DELTA * sl) {
    if (map_len(r->left) > SZ_WBT_GAMMA * map_len(r->right)) {
      SzMap *nr = map_rot_right(r->key, r->val, r->left, r->right, kind);
      SzMap *out = map_rot_left(k, v, l, nr, kind);
      sz_release(nr);
      return out;
    }
    return map_rot_left(k, v, l, r, kind);
  }
  if (sl > SZ_WBT_DELTA * sr) {
    if (map_len(l->right) > SZ_WBT_GAMMA * map_len(l->left)) {
      SzMap *nl = map_rot_left(l->key, l->val, l->left, l->right, kind);
      SzMap *out = map_rot_right(k, v, nl, r, kind);
      sz_release(nl);
      return out;
    }
    return map_rot_right(k, v, l, r, kind);
  }
  return map_node(k, v, l, r, kind);
}

SzMap *sz_map_set(SzMap *m, void *k, void *v, int32_t kind) {
  int c;
  SzMap *child;
  SzMap *out;
  (void)kind;
  if (!m)
    return map_node(k, v, NULL, NULL, sz_map_infer_key_kind(k));
  c = map_cmp(m, k);
  if (c == 0)
    return map_node(k, v, m->left, m->right, m->key_kind);
  if (c > 0) {
    child = sz_map_set(m->left, k, v, kind);
    out = map_balance(m->key, m->val, child, m->right, m->key_kind);
    sz_release(child);
    return out;
  }
  child = sz_map_set(m->right, k, v, kind);
  out = map_balance(m->key, m->val, m->left, child, m->key_kind);
  sz_release(child);
  return out;
}

void *sz_map_get_or(SzMap *m, void *k, void *dflt) {
  while (m) {
    int c = map_cmp(m, k);
    if (c == 0)
      return m->val;
    m = c > 0 ? m->left : m->right;
  }
  return dflt;
}

void *sz_map_get(SzMap *m, void *k) {
  while (m) {
    int c = map_cmp(m, k);
    if (c == 0)
      return sz_adt_new(1, m->val);
    m = c > 0 ? m->left : m->right;
  }
  return sz_adt_new(0, NULL);
}

int64_t sz_map_contains(SzMap *m, void *k) {
  while (m) {
    int c = map_cmp(m, k);
    if (c == 0)
      return 1;
    m = c > 0 ? m->left : m->right;
  }
  return 0;
}

static SzMap *map_min(SzMap *m) {
  while (m && m->left)
    m = m->left;
  return m;
}

static SzMap *map_max(SzMap *m) {
  while (m && m->right)
    m = m->right;
  return m;
}

/* Drop the smallest key. Borrowed in, +1 out. `m` is non-null. */
static SzMap *map_drop_min(SzMap *m) {
  SzMap *child;
  SzMap *out;
  if (!m->left) {
    sz_retain(m->right);
    return m->right;
  }
  child = map_drop_min(m->left);
  out = map_balance(m->key, m->val, child, m->right, m->key_kind);
  sz_release(child);
  return out;
}

/* Drop the largest key. Borrowed in, +1 out. `m` is non-null. */
static SzMap *map_drop_max(SzMap *m) {
  SzMap *child;
  SzMap *out;
  if (!m->right) {
    sz_retain(m->left);
    return m->left;
  }
  child = map_drop_max(m->right);
  out = map_balance(m->key, m->val, m->left, child, m->key_kind);
  sz_release(child);
  return out;
}

/* Merge two balanced trees. Every key of `l` precedes every key of `r`.
 * Borrowed in, +1 out. */
static SzMap *map_glue(SzMap *l, SzMap *r, int32_t kind) {
  SzMap *pivot;
  SzMap *rest;
  SzMap *out;
  if (!l) {
    sz_retain(r);
    return r;
  }
  if (!r) {
    sz_retain(l);
    return l;
  }
  if (l->size > r->size) {
    pivot = map_max(l);
    rest = map_drop_max(l);
    out = map_balance(pivot->key, pivot->val, rest, r, kind);
    sz_release(rest);
    return out;
  }
  pivot = map_min(r);
  rest = map_drop_min(r);
  out = map_balance(pivot->key, pivot->val, l, rest, kind);
  sz_release(rest);
  return out;
}

SzMap *sz_map_remove(SzMap *m, void *k) {
  int c;
  SzMap *child;
  SzMap *out;
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
    out = map_balance(m->key, m->val, child, m->right, m->key_kind);
    sz_release(child);
    return out;
  }
  if (c < 0) {
    child = sz_map_remove(m->right, k);
    if (child == m->right) {
      sz_release(child);
      sz_retain(m);
      return m;
    }
    out = map_balance(m->key, m->val, m->left, child, m->key_kind);
    sz_release(child);
    return out;
  }
  return map_glue(m->left, m->right, m->key_kind);
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

int64_t sz_map_size(SzMap *m) { return map_len(m); }

/* Walk `b` in order and set each key into `cur`. Consumes `cur` (+1).
 * Returns the new tree (+1). `take_b_val` copies values; else NULL. */
static SzMap *map_overlay_walk(SzMap *cur, const SzMap *b, int take_b_val) {
  SzMap *next;
  if (!b)
    return cur;
  cur = map_overlay_walk(cur, b->left, take_b_val);
  next = sz_map_set(cur, b->key, take_b_val ? b->val : NULL, 0);
  sz_release(cur);
  cur = next;
  return map_overlay_walk(cur, b->right, take_b_val);
}

/* Overlay `b`'s keys onto `a`. `take_b_val` copies values from `b`. */
static SzMap *map_overlay(SzMap *a, SzMap *b, int take_b_val) {
  if (!b) {
    sz_retain(a);
    return a;
  }
  if (!a) {
    sz_retain(b);
    return b;
  }
  sz_retain(a);
  return map_overlay_walk(a, b, take_b_val);
}

/* Walk `a` in order and keep keys that are (or are not) in `b`.
 * Consumes `out` (+1). Returns the new tree (+1). */
static SzMap *map_keep_walk(SzMap *out, const SzMap *a, SzMap *b,
                            int want_in_b, int take_a_val) {
  SzMap *next;
  if (!a)
    return out;
  out = map_keep_walk(out, a->left, b, want_in_b, take_a_val);
  if ((sz_map_contains(b, a->key) != 0) == want_in_b) {
    next = sz_map_set(out, a->key, take_a_val ? a->val : NULL, 0);
    sz_release(out);
    out = next;
  }
  return map_keep_walk(out, a->right, b, want_in_b, take_a_val);
}

/* Keep keys of `a` that are (or are not) in `b`. `take_a_val` copies values. */
static SzMap *map_keep(SzMap *a, SzMap *b, int want_in_b, int take_a_val) {
  if (!a)
    return NULL;
  if (!b) {
    if (want_in_b)
      return NULL;
    sz_retain(a);
    return a;
  }
  return map_keep_walk(NULL, a, b, want_in_b, take_a_val);
}

SzMap *sz_set_union(SzMap *a, SzMap *b) { return map_overlay(a, b, 0); }

SzMap *sz_set_intersect(SzMap *a, SzMap *b) { return map_keep(a, b, 1, 0); }

SzMap *sz_set_diff(SzMap *a, SzMap *b) { return map_keep(a, b, 0, 0); }

SzMap *sz_map_union(SzMap *a, SzMap *b) { return map_overlay(a, b, 1); }

SzMap *sz_map_intersect(SzMap *a, SzMap *b) { return map_keep(a, b, 1, 1); }

SzMap *sz_map_diff(SzMap *a, SzMap *b) { return map_keep(a, b, 0, 1); }

/* Walk `m` in order and keep entries that pass `pred`. `on_key` 1 tests
 * the key; else the value. Consumes `out` (+1). Returns the new tree. */
static SzMap *map_filter_walk(SzMap *out, const SzMap *m, SzListPred pred,
                              void *env, int on_key) {
  SzMap *next;
  if (!m)
    return out;
  out = map_filter_walk(out, m->left, pred, env, on_key);
  if (pred(on_key ? m->key : m->val, env) != 0) {
    next = sz_map_set(out, m->key, m->val, 0);
    sz_release(out);
    out = next;
  }
  return map_filter_walk(out, m->right, pred, env, on_key);
}

/* Filter entries. `on_key` 1 tests the key; else the value. */
static SzMap *map_filter_on(SzMap *m, SzListPred pred, void *env, int on_key,
                            const char *panic) {
  if (!pred)
    sz_panic(panic);
  if (!m)
    return NULL;
  return map_filter_walk(NULL, m, pred, env, on_key);
}

/* 1 when some entry of `m` has (pred(...) != 0) == stop_on. Stops early. */
static int map_pred_scan(const SzMap *m, SzListPred pred, void *env,
                         int on_key, int stop_on) {
  if (!m)
    return 0;
  if (map_pred_scan(m->left, pred, env, on_key, stop_on))
    return 1;
  if ((pred(on_key ? m->key : m->val, env) != 0) == stop_on)
    return 1;
  return map_pred_scan(m->right, pred, env, on_key, stop_on);
}

/* Test entries. `on_key` 1 tests the key; else the value. */
static int64_t map_pred_on(SzMap *m, SzListPred pred, void *env, int on_key,
                           int want_all, const char *panic) {
  if (!pred)
    sz_panic(panic);
  if (want_all)
    return map_pred_scan(m, pred, env, on_key, 0) ? 0 : 1;
  return map_pred_scan(m, pred, env, on_key, 1);
}

SzMap *sz_map_filter(SzMap *m, SzListPred pred, void *env) {
  return map_filter_on(m, pred, env, 0, "sz_map_filter(null pred)");
}

/* Walk `m` in order and map each value. Consumes `out` (+1). */
static SzMap *map_values_walk(SzMap *out, const SzMap *m, SzListMapFn fn,
                              void *env) {
  SzMap *next;
  void *nv;
  if (!m)
    return out;
  out = map_values_walk(out, m->left, fn, env);
  /* Mapper returns +1. Set retains. Drop the mapper ref. */
  nv = fn(m->val, env);
  next = sz_map_set(out, m->key, nv, 0);
  sz_release(nv);
  sz_release(out);
  return map_values_walk(next, m->right, fn, env);
}

SzMap *sz_map_map_values(SzMap *m, SzListMapFn fn, void *env) {
  if (!fn)
    sz_panic("sz_map_map_values(null fn)");
  if (!m)
    return NULL;
  return map_values_walk(NULL, m, fn, env);
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

/* Walk `s` in order and map each key. Consumes `out` (+1). The first
 * insert into the empty tree infers the key kind. */
static SzMap *set_map_walk(SzMap *out, const SzMap *s, SzListMapFn fn,
                           void *env) {
  SzMap *next;
  void *nk;
  if (!s)
    return out;
  out = set_map_walk(out, s->left, fn, env);
  /* Mapper returns +1. Set retains. Drop the mapper ref. */
  nk = fn(s->key, env);
  next = sz_map_set(out, nk, NULL, 0);
  sz_release(nk);
  sz_release(out);
  return set_map_walk(next, s->right, fn, env);
}

SzMap *sz_set_map(SzMap *s, SzListMapFn fn, void *env, int32_t key_kind) {
  (void)key_kind;
  if (!fn)
    sz_panic("sz_set_map(null fn)");
  if (!s)
    return NULL;
  return set_map_walk(NULL, s, fn, env);
}

int64_t sz_set_exists(SzMap *s, SzListPred pred, void *env) {
  return map_pred_on(s, pred, env, 1, 0, "sz_set_exists(null pred)");
}

int64_t sz_set_forall(SzMap *s, SzListPred pred, void *env) {
  return map_pred_on(s, pred, env, 1, 1, "sz_set_forall(null pred)");
}

/* 1 when some key of `a` breaks the rule: in `b` when want_in_b, or out
 * of `b` when !want_in_b. Stops early. */
static int set_keys_violation(const SzMap *a, SzMap *b, int want_in_b) {
  if (!a)
    return 0;
  if (set_keys_violation(a->left, b, want_in_b))
    return 1;
  if ((sz_map_contains(b, a->key) != 0) != want_in_b)
    return 1;
  return set_keys_violation(a->right, b, want_in_b);
}

int64_t sz_set_is_subset(SzMap *a, SzMap *b) {
  return set_keys_violation(a, b, 1) ? 0 : 1;
}

int64_t sz_set_is_disjoint(SzMap *a, SzMap *b) {
  if (!a || !b)
    return 1;
  return set_keys_violation(a, b, 0) ? 0 : 1;
}
