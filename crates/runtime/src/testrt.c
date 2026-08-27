#include "scuzz_rt.h"
#include "scuzz_ui.h"
#include "rt_util.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TestRuntime: install / reset fake interpreters. */

void sz_testrt_clock_reset_live(void);
void sz_testrt_random_reset_live(void);
static void sz_testrt_fs_reset_live(void);
static void sz_testrt_net_reset_live(void);
static void sz_testrt_net_install(void);

/* --- mem FS -------------------------------------------------------------- */

typedef struct MemNode {
  char *path; /* normalized, no trailing slash (except root "") */
  int is_dir;
  char *data;
  size_t len;
  struct MemNode *next;
} MemNode;

static int g_fs_fake = 0;
static MemNode *g_fs = NULL;

static void sz_testrt_fs_reset_live(void) {
  MemNode *n = g_fs;
  while (n) {
    MemNode *next = n->next;
    sz_free(n->path);
    sz_free(n->data);
    sz_free(n);
    n = next;
  }
  g_fs = NULL;
  g_fs_fake = 0;
}

static void sz_testrt_fs_install(void) {
  sz_testrt_fs_reset_live();
  g_fs_fake = 1;
  /* Root directory exists. */
  {
    MemNode *root = (MemNode *)sz_alloc(sizeof(MemNode));
    root->path = (char *)sz_alloc(1);
    root->path[0] = '\0';
    root->is_dir = 1;
    root->data = NULL;
    root->len = 0;
    root->next = NULL;
    g_fs = root;
  }
}

int sz_testrt_fs_is_fake(void) { return g_fs_fake; }

static char *norm_path(const char *p) {
  size_t n;
  char *out;
  size_t i, j;
  if (!p)
    p = "";
  /* Strip leading ./ and collapse duplicate slashes; drop trailing slash. */
  while (p[0] == '.' && p[1] == '/')
    p += 2;
  n = strlen(p);
  out = (char *)sz_alloc(n + 1);
  j = 0;
  for (i = 0; i < n; i++) {
    if (p[i] == '/' && j > 0 && out[j - 1] == '/')
      continue;
    out[j++] = p[i];
  }
  while (j > 0 && out[j - 1] == '/')
    j--;
  out[j] = '\0';
  /* Map a lone '.' to the root path. */
  if (strcmp(out, ".") == 0)
    out[0] = '\0';
  return out;
}

static MemNode *fs_find(const char *path) {
  MemNode *n;
  for (n = g_fs; n; n = n->next) {
    if (strcmp(n->path, path) == 0)
      return n;
  }
  return NULL;
}

static MemNode *fs_ensure_dir(const char *path) {
  MemNode *n = fs_find(path);
  if (n)
    return n->is_dir ? n : NULL;
  n = (MemNode *)sz_alloc(sizeof(MemNode));
  n->path = (char *)sz_alloc(strlen(path) + 1);
  memcpy(n->path, path, strlen(path) + 1);
  n->is_dir = 1;
  n->data = NULL;
  n->len = 0;
  n->next = g_fs;
  g_fs = n;
  return n;
}

static int parent_path(const char *path, char *out, size_t out_sz) {
  const char *slash = strrchr(path, '/');
  size_t n;
  if (!slash) {
    if (out_sz < 1)
      return 0;
    out[0] = '\0';
    return 1;
  }
  n = (size_t)(slash - path);
  if (n + 1 > out_sz)
    return 0;
  memcpy(out, path, n);
  out[n] = '\0';
  return 1;
}

typedef struct {
  int is_err;
  union {
    SzError *err;
    void *ok;
  } as;
} BoxResult;

/* DELAY wraps `r` in PURE (extra retain at run). Drop that retain here. */
static SzIo *unwrap_box(void *value, void *env) {
  (void)env;
  BoxResult *r = (BoxResult *)value;
  if (!r)
    return sz_io_fail_cstr("TestRuntime: null result");
  if (r->is_err) {
    SzError *err = r->as.err;
    r->as.err = NULL;
    sz_release(r);
    return fail_drop(err);
  }
  {
    void *ok = r->as.ok;
    r->as.ok = NULL;
    sz_release(r);
    return pure_drop(ok);
  }
}

/* Fault plan: SCUZZ_FAULT_KIND + SCUZZ_FAULT_N + SCUZZ_FAULT_MODE, else
 * SCUZZ_FAULT_SEED. Seed 0 / unset is no fault. Seed k>0 decodes as
 * kind=(k-1)%3, mode=((k-1)/3)%3, n=(((k-1)/3)/3)%16+1. */
static int g_fault_kind;
static int g_fault_mode;
static int g_fault_n;
static int g_fault_seed;
static int g_fault_count_fs;
static int g_fault_count_net;
static int g_fault_count_queue;
static const char *g_fault_fail_msg;
static int g_idle_have;
static size_t g_idle_bytes;
static size_t g_idle_count;
static int g_heap_have;
static size_t g_heap_bytes;
static size_t g_heap_count;
static uint64_t g_heap_rc;
static int g_sess_have;
static size_t g_sess_bytes;
static size_t g_sess_count;
static uint64_t g_sess_rc;
static int g_skip_orphan_cancel;
static int g_skip_unstepped_ensure;

static int fault_kind_from_name(const char *s) {
  if (!s || !s[0])
    return 0;
  if (strcmp(s, "fs") == 0)
    return SZ_FAULT_FS;
  if (strcmp(s, "net") == 0)
    return SZ_FAULT_NET;
  if (strcmp(s, "queue") == 0)
    return SZ_FAULT_QUEUE;
  return 0;
}

static int fault_mode_from_name(const char *s) {
  if (!s || !s[0])
    return SZ_FAULT_FAIL;
  if (strcmp(s, "drop") == 0)
    return SZ_FAULT_DROP;
  if (strcmp(s, "corrupt") == 0)
    return SZ_FAULT_CORRUPT;
  return SZ_FAULT_FAIL;
}

static void fault_decode_seed(int seed) {
  int idx;
  int rest;
  if (seed <= 0) {
    g_fault_kind = 0;
    g_fault_mode = SZ_FAULT_FAIL;
    g_fault_n = 0;
    g_fault_seed = 0;
    return;
  }
  g_fault_seed = seed;
  idx = seed - 1;
  g_fault_kind = (idx % 3) + 1;
  rest = idx / 3;
  g_fault_mode = rest % 3;
  g_fault_n = (rest / 3) % 16 + 1;
}

static void fault_arm_from_env(void) {
  const char *kind;
  const char *n_s;
  const char *seed_s;
  g_fault_kind = 0;
  g_fault_mode = SZ_FAULT_FAIL;
  g_fault_n = 0;
  g_fault_seed = 0;
  g_fault_count_fs = 0;
  g_fault_count_net = 0;
  g_fault_count_queue = 0;
  g_fault_fail_msg = NULL;
  g_idle_have = 0;
  g_idle_bytes = 0;
  g_idle_count = 0;
  kind = getenv("SCUZZ_FAULT_KIND");
  n_s = getenv("SCUZZ_FAULT_N");
  seed_s = getenv("SCUZZ_FAULT_SEED");
  if (kind && kind[0]) {
    g_fault_kind = fault_kind_from_name(kind);
    g_fault_n = n_s && n_s[0] ? atoi(n_s) : 1;
    if (g_fault_n < 1)
      g_fault_n = 1;
    g_fault_mode = fault_mode_from_name(getenv("SCUZZ_FAULT_MODE"));
    g_fault_seed = 0;
    return;
  }
  if (seed_s && seed_s[0])
    fault_decode_seed(atoi(seed_s));
}

int sz_testrt_fault_tick(int kind) {
  int *c;
  if (!g_fault_kind || g_fault_n <= 0 || kind != g_fault_kind)
    return 0;
  if (kind == SZ_FAULT_FS)
    c = &g_fault_count_fs;
  else if (kind == SZ_FAULT_NET)
    c = &g_fault_count_net;
  else if (kind == SZ_FAULT_QUEUE)
    c = &g_fault_count_queue;
  else
    return 0;
  *c += 1;
  return *c == g_fault_n;
}

static int sz_testrt_fault_mode(void) { return g_fault_mode; }

void sz_testrt_fault_note(const char *msg) { g_fault_fail_msg = msg; }

const char *sz_testrt_fault_take_msg(void) {
  const char *m = g_fault_fail_msg;
  g_fault_fail_msg = NULL;
  return m;
}

int sz_testrt_oracles_armed(void) {
  const char *tr = getenv("SCUZZ_TESTRT");
  return tr && tr[0] == '1';
}

void sz_testrt_ui_idle_snapshot(void) {
  sz_alloc_stats(&g_idle_bytes, &g_idle_count);
  g_idle_have = 1;
}

void sz_testrt_ui_idle_reset(void) {
  g_idle_have = 0;
}

void sz_testrt_ui_idle_check(void) {
  size_t b = 0;
  size_t c = 0;
  if (!sz_testrt_oracles_armed() || !g_idle_have)
    return;
  sz_alloc_stats(&b, &c);
  if (c > g_idle_count || b > g_idle_bytes) {
    fprintf(stderr,
            "scuzz: leak: heap grew across idle UI loop (count %zu -> %zu, "
            "bytes %zu -> %zu)\n",
            g_idle_count, c, g_idle_bytes, b);
    sz_panic("leak: heap grew across idle UI loop");
  }
}

static void baseline_fail(const char *kind, size_t base_c, size_t c,
                          size_t base_b, size_t b, uint64_t base_rc,
                          uint64_t rc) {
  if (c > base_c || b > base_b) {
    fprintf(stderr,
            "scuzz: heap baseline: %s grew (count %zu -> %zu, bytes %zu -> "
            "%zu)\n",
            kind, base_c, c, base_b, b);
    sz_panic("heap baseline: live heap grew");
  }
  if (rc > base_rc) {
    fprintf(stderr,
            "scuzz: unpaired acquire: leftover retain (%s rc %llu -> %llu)\n",
            kind, (unsigned long long)base_rc, (unsigned long long)rc);
    sz_panic("unpaired acquire: leftover retain");
  }
}

void sz_testrt_heap_baseline_snapshot(void) {
  if (!sz_testrt_oracles_armed())
    return;
  sz_alloc_stats(&g_heap_bytes, &g_heap_count);
  g_heap_rc = sz_alloc_rc_sum();
  g_heap_have = 1;
}

void sz_testrt_heap_baseline_check(void) {
  size_t b = 0;
  size_t c = 0;
  uint64_t rc;
  if (!sz_testrt_oracles_armed() || !g_heap_have)
    return;
  sz_alloc_stats(&b, &c);
  rc = sz_alloc_rc_sum();
  baseline_fail("process", g_heap_count, c, g_heap_bytes, b, g_heap_rc, rc);
}

void sz_testrt_session_baseline_snapshot(void) {
  if (!sz_testrt_oracles_armed())
    return;
  sz_alloc_stats(&g_sess_bytes, &g_sess_count);
  g_sess_rc = sz_alloc_rc_sum();
  g_sess_have = 1;
}

void sz_testrt_session_baseline_check(void) {
  size_t b = 0;
  size_t c = 0;
  uint64_t rc;
  if (!sz_testrt_oracles_armed() || !g_sess_have)
    return;
  sz_alloc_stats(&b, &c);
  rc = sz_alloc_rc_sum();
  /* A UI session legitimately ends holding changed state (typed text, added
     items): a replaced block keeps the count level while bytes drift. Leaks
     surface as net block-count or retain-sum growth. Byte drift across events
     is not leakage; the idle-pump leak oracle keeps bytes strict where no
     events run. */
  if (c > g_sess_count) {
    fprintf(stderr,
            "scuzz: heap baseline: session leaked blocks (count %zu -> %zu, "
            "bytes %zu -> %zu)\n",
            g_sess_count, c, g_sess_bytes, b);
    sz_panic("heap baseline: session leaked live blocks");
  }
  if (rc > g_sess_rc) {
    fprintf(stderr,
            "scuzz: unpaired acquire: leftover retain (session rc %llu -> "
            "%llu)\n",
            (unsigned long long)g_sess_rc, (unsigned long long)rc);
    sz_panic("unpaired acquire: leftover retain");
  }
}

void sz_testrt_plant_skip_orphan_cancel(void) { g_skip_orphan_cancel = 1; }

void sz_testrt_plant_skip_unstepped_ensure(void) {
  g_skip_unstepped_ensure = 1;
}

int sz_testrt_skip_orphan_cancel(void) { return g_skip_orphan_cancel; }

int sz_testrt_skip_unstepped_ensure(void) { return g_skip_unstepped_ensure; }

void sz_effect_log(const char *line) {
  sz_timeline_log_cstr(line ? line : "", "");
}

static void fs_log(const char *op) {
  char buf[48];
  snprintf(buf, sizeof buf, "Fs.%s", op);
  sz_timeline_log_cstr(buf, "");
}

static int fs_fault(BoxResult *r) {
  if (!sz_testrt_fault_tick(SZ_FAULT_FS))
    return 0;
  r->is_err = 1;
  r->as.err = sz_error_new(2, "Fs: injected fault");
  return 1;
}

static SzIo *testrt_fs_bind(SzString *path, SzThunk thunk) {
  SzPair *pack = sz_pair_new(path, NULL);
  SzIo *io = fm_drop(sz_io_delay(thunk, pack), unwrap_box, NULL);
  sz_release(pack);
  return io;
}

static void *mem_read(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *n;
  sz_timeline_log_cstr("Fs.read", path_s ? sz_string_cstr(path_s) : "");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  n = fs_find(path);
  sz_free(path);
  if (!n || n->is_dir) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: not found (mem)");
    goto done;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_bytes(n->data ? n->data : "", n->len);
done:
  return r;
}

SzIo *sz_testrt_fs_read(SzString *path) {
  return testrt_fs_bind(path, mem_read);
}

static void *mem_write(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = (SzString *)pack->left;
  SzString *contents = (SzString *)pack->right;
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  char parent[1024];
  MemNode *n;
  SzString *c;
  sz_timeline_log_bytes("Fs.write", contents ? contents->data : "",
                        contents ? contents->len : 0);
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  c = contents ? contents : sz_string_from_cstr("");

  if (!parent_path(path, parent, sizeof parent)) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.write: path too long (mem)");
    goto done;
  }
  if (parent[0] != '\0') {
    MemNode *pnode = fs_find(parent);
    if (!pnode) {
      sz_free(path);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.write: no parent (mem)");
      goto done;
    }
    if (!pnode->is_dir) {
      sz_free(path);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.write: parent is file (mem)");
      goto done;
    }
  }

  n = fs_find(path);
  if (n && n->is_dir) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.write: is a directory (mem)");
    goto done;
  }
  if (!n) {
    n = (MemNode *)sz_alloc(sizeof(MemNode));
    n->path = path;
    n->is_dir = 0;
    n->data = NULL;
    n->len = 0;
    n->next = g_fs;
    g_fs = n;
  } else {
    sz_free(path);
    sz_free(n->data);
    n->data = NULL;
    n->len = 0;
  }
  n->len = c->len;
  n->data = (char *)sz_alloc(c->len + 1);
  if (c->len)
    memcpy(n->data, c->data, c->len);
  n->data[c->len] = '\0';
  r->is_err = 0;
  r->as.ok = NULL;
done:
  if (!contents)
    sz_release(c);
  return r;
}

SzIo *sz_testrt_fs_write(SzString *path, SzString *contents) {
  SzPair *pack = sz_pair_new(path, contents);
  SzIo *io = fm_drop(sz_io_delay(mem_write, pack), unwrap_box, NULL);
  sz_release(pack);
  return io;
}

static int is_direct_child(const char *dir, const char *child) {
  size_t dlen = strlen(dir);
  const char *rest;
  if (dlen == 0) {
    rest = child;
  } else {
    if (strncmp(child, dir, dlen) != 0 || child[dlen] != '/')
      return 0;
    rest = child + dlen + 1;
  }
  return rest[0] != '\0' && strchr(rest, '/') == NULL;
}

static void *mem_list(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *dir;
  MemNode *n;
  SzList *acc;
  fs_log("list");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  dir = fs_find(path);

  if (!dir || !dir->is_dir) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.list: not a directory (mem)");
    goto done;
  }
  acc = sz_list_nil();
  for (n = g_fs; n; n = n->next) {
    if (n == dir)
      continue;
    if (is_direct_child(path, n->path)) {
      const char *base = strrchr(n->path, '/');
      base = base ? base + 1 : n->path;
      {
        SzString *s = sz_string_from_cstr(base);
        void *flag = sz_box_i64(n->is_dir ? 1 : 0);
        SzPair *ent = sz_pair_new(s, flag);
        SzList *old = acc;
        acc = sz_list_cons(ent, old);
        sz_release(s);
        sz_release(flag);
        sz_release(ent);
        sz_release(old);
      }
    }
  }
  sz_free(path);
  r->is_err = 0;
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    r->as.ok = rev;
  }
done:
  return r;
}

SzIo *sz_testrt_fs_list(SzString *path) {
  return testrt_fs_bind(path, mem_list);
}

static void *mem_exists(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *n;
  fs_log("exists");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  n = fs_find(path);
  sz_free(path);
  r->is_err = 0;
  r->as.ok = sz_box_i64(n ? 1 : 0);
  return r;
}

SzIo *sz_testrt_fs_exists(SzString *path) {
  return testrt_fs_bind(path, mem_exists);
}

static int is_self_or_under(const char *dir, const char *path) {
  size_t dlen = strlen(dir);
  if (strcmp(dir, path) == 0)
    return 1;
  if (dlen == 0)
    return path[0] != '\0';
  return strncmp(path, dir, dlen) == 0 && path[dlen] == '/';
}

static int is_under(const char *dir, const char *child) {
  size_t dlen = strlen(dir);
  if (dlen == 0)
    return child[0] != '\0';
  return strncmp(child, dir, dlen) == 0 && child[dlen] == '/';
}

static void mem_free_node(MemNode *n) {
  sz_free(n->path);
  sz_free(n->data);
  sz_free(n);
}

static void *mem_delete(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *n;
  MemNode **pp;
  fs_log("delete");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  if (!path[0]) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.delete: refused root (mem)");
    goto done;
  }
  n = fs_find(path);
  if (!n) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.delete: not found (mem)");
    goto done;
  }
  pp = &g_fs;
  while (*pp) {
    MemNode *cur = *pp;
    if (is_self_or_under(path, cur->path)) {
      *pp = cur->next;
      mem_free_node(cur);
    } else {
      pp = &cur->next;
    }
  }
  sz_free(path);
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

SzIo *sz_testrt_fs_delete(SzString *path) {
  return testrt_fs_bind(path, mem_delete);
}

static void *mem_rename(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *from_s = pack ? (SzString *)pack->left : NULL;
  SzString *to_s = pack ? (SzString *)pack->right : NULL;
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *from;
  char *to;
  char parent[1024];
  MemNode *src;
  MemNode *n;
  size_t from_len;
  fs_log("rename");
  if (fs_fault(r))
    return r;
  from = norm_path(sz_string_cstr(from_s));
  to = norm_path(sz_string_cstr(to_s));
  if (!from[0] || !to[0]) {
    sz_free(from);
    sz_free(to);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: refused root (mem)");
    goto done;
  }
  src = fs_find(from);
  if (!src) {
    sz_free(from);
    sz_free(to);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: not found (mem)");
    goto done;
  }
  if (fs_find(to)) {
    sz_free(from);
    sz_free(to);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: dest exists (mem)");
    goto done;
  }
  if (!parent_path(to, parent, sizeof parent)) {
    sz_free(from);
    sz_free(to);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: path too long (mem)");
    goto done;
  }
  if (parent[0] != '\0') {
    MemNode *pnode = fs_find(parent);
    if (!pnode || !pnode->is_dir) {
      sz_free(from);
      sz_free(to);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.rename: no parent (mem)");
      goto done;
    }
  }
  if (src->is_dir && is_under(from, to)) {
    sz_free(from);
    sz_free(to);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: into self (mem)");
    goto done;
  }
  from_len = strlen(from);
  for (n = g_fs; n; n = n->next) {
    char *np;
    size_t suffix_len;
    if (!is_self_or_under(from, n->path))
      continue;
    suffix_len = strlen(n->path) - from_len;
    np = (char *)sz_alloc(strlen(to) + suffix_len + 1);
    memcpy(np, to, strlen(to));
    memcpy(np + strlen(to), n->path + from_len, suffix_len + 1);
    sz_free(n->path);
    n->path = np;
  }
  sz_free(from);
  sz_free(to);
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

SzIo *sz_testrt_fs_rename(SzString *from, SzString *to) {
  SzPair *pack = sz_pair_new(from, to);
  SzIo *io = fm_drop(sz_io_delay(mem_rename, pack), unwrap_box, NULL);
  sz_release(pack);
  return io;
}

static void *mem_walk(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *dir;
  MemNode *n;
  SzList *acc;
  size_t dlen;
  fs_log("walk");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  dir = fs_find(path);
  if (!dir || !dir->is_dir) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.walk: not a directory (mem)");
    goto done;
  }
  acc = sz_list_nil();
  dlen = strlen(path);
  for (n = g_fs; n; n = n->next) {
    const char *rel;
    if (n == dir)
      continue;
    if (!is_under(path, n->path))
      continue;
    rel = dlen == 0 ? n->path : n->path + dlen + 1;
    {
      SzString *s = sz_string_from_cstr(rel);
      void *flag = sz_box_i64(n->is_dir ? 1 : 0);
      SzPair *ent = sz_pair_new(s, flag);
      SzList *old = acc;
      acc = sz_list_cons(ent, old);
      sz_release(s);
      sz_release(flag);
      sz_release(ent);
      sz_release(old);
    }
  }
  sz_free(path);
  r->is_err = 0;
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    r->as.ok = rev;
  }
done:
  return r;
}

SzIo *sz_testrt_fs_walk(SzString *path) {
  return testrt_fs_bind(path, mem_walk);
}

static void *mem_mkdirs(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  char tmp[1024];
  size_t len;
  size_t i;
  fs_log("mkdirs");
  if (fs_fault(r))
    return r;
  path = norm_path(sz_string_cstr(path_s));
  len = strlen(path);

  if (len == 0) {
    sz_free(path);
    r->is_err = 0;
    r->as.ok = NULL;
    goto done;
  }
  if (len >= sizeof tmp) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: path too long (mem)");
    goto done;
  }
  memcpy(tmp, path, len + 1);
  for (i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      if (!fs_ensure_dir(tmp)) {
        sz_free(path);
        r->is_err = 1;
        r->as.err = sz_error_new(2, "Fs.mkdirs: path component is file (mem)");
        goto done;
      }
      tmp[i] = '/';
    }
  }
  if (!fs_ensure_dir(path)) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: path is file (mem)");
    goto done;
  }
  sz_free(path);
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

SzIo *sz_testrt_fs_mkdirs(SzString *path) {
  return testrt_fs_bind(path, mem_mkdirs);
}

static char *canon_path(const char *p) {
  char *norm = norm_path(p);
  char *parts[256];
  size_t nparts = 0;
  size_t i = 0;
  int abs = 0;
  char *out;
  size_t out_len = 1;
  if (norm[0] == '/')
    abs = 1;
  while (norm[i]) {
    size_t start = i;
    while (norm[i] && norm[i] != '/')
      i++;
    if (i > start) {
      size_t n = i - start;
      char *seg = (char *)sz_alloc(n + 1);
      memcpy(seg, norm + start, n);
      seg[n] = '\0';
      if (strcmp(seg, ".") == 0) {
        sz_free(seg);
      } else if (strcmp(seg, "..") == 0) {
        sz_free(seg);
        if (nparts > 0) {
          sz_free(parts[nparts - 1]);
          nparts--;
        }
      } else if (nparts < 256) {
        parts[nparts++] = seg;
      } else {
        /* Fail when the path has too many segments. Do not drop extra segments. */
        size_t k;
        sz_free(seg);
        sz_free(norm);
        for (k = 0; k < nparts; k++)
          sz_free(parts[k]);
        return NULL;
      }
    }
    if (norm[i] == '/')
      i++;
  }
  sz_free(norm);
  for (i = 0; i < nparts; i++)
    out_len += strlen(parts[i]) + 1;
  out = (char *)sz_alloc(out_len + 2);
  out[0] = '\0';
  if (abs)
    strcat(out, "/");
  for (i = 0; i < nparts; i++) {
    if (i > 0)
      strcat(out, "/");
    strcat(out, parts[i]);
    sz_free(parts[i]);
  }
  if (abs && nparts == 0) {
    out[0] = '/';
    out[1] = '\0';
  }
  return out;
}

static void *mem_canonicalize(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path_s = pack_path(pack);
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  char *path;
  MemNode *n;
  fs_log("canonicalize");
  if (fs_fault(r))
    return r;
  path = canon_path(sz_string_cstr(path_s));
  if (!path) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.canonicalize: path too deep (mem)");
    goto done;
  }
  n = fs_find(path);
  if (!n) {
    /* Allow canonicalize of a missing leaf if the parent exists as a dir.
       Match common realpath failure modes loosely: need an exact mem node. */
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.canonicalize: not found (mem)");
    sz_free(path);
    goto done;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(path);
  sz_free(path);
done:
  return r;
}

SzIo *sz_testrt_fs_canonicalize(SzString *path) {
  return testrt_fs_bind(path, mem_canonicalize);
}

/* --- stub network -------------------------------------------------------- */

typedef struct NetStub {
  char *url;
  char *body;
  struct NetStub *next;
} NetStub;

static int g_net_fake = 0;
static NetStub *g_stubs = NULL;
enum { NET_REQ_CAP = 16 };
static char *g_req[NET_REQ_CAP];
static int g_req_n = 0;
static char *g_last_serve_body = NULL;

static void net_req_clear(void) {
  int i;
  for (i = 0; i < g_req_n; i++)
    sz_free(g_req[i]);
  g_req_n = 0;
}

static void net_req_push(const char *path) {
  size_t n;
  if (g_req_n >= NET_REQ_CAP)
    sz_panic("Net: too many injected requests");
  if (!path || !path[0])
    path = "/";
  n = strlen(path);
  g_req[g_req_n] = (char *)sz_alloc(n + 1);
  memcpy(g_req[g_req_n], path, n + 1);
  g_req_n++;
}

static void net_clear_serve(void) {
  net_req_clear();
  sz_free(g_last_serve_body);
  g_last_serve_body = NULL;
}

static void sz_testrt_net_reset_live(void) {
  NetStub *s = g_stubs;
  while (s) {
    NetStub *n = s->next;
    sz_free(s->url);
    sz_free(s->body);
    sz_free(s);
    s = n;
  }
  g_stubs = NULL;
  net_clear_serve();
  g_net_fake = 0;
}

void sz_testrt_net_inject_request(const char *path) {
  if (!g_net_fake)
    sz_testrt_net_install();
  net_req_clear();
  net_req_push(path);
}

void sz_testrt_net_queue_request(const char *path) {
  if (!g_net_fake)
    sz_testrt_net_install();
  net_req_push(path);
}

int sz_testrt_net_serve_pending(void) { return g_req_n; }

char *sz_testrt_net_pop_request(void) {
  char *p;
  int i;
  if (g_req_n <= 0)
    return NULL;
  p = g_req[0];
  for (i = 1; i < g_req_n; i++)
    g_req[i - 1] = g_req[i];
  g_req_n--;
  return p;
}

static void sz_testrt_net_install(void) {
  sz_testrt_net_reset_live();
  g_net_fake = 1;
  sz_testrt_net_inject_request("/");
}

int sz_testrt_net_is_fake(void) { return g_net_fake; }

void sz_testrt_net_stub(const char *url, const char *body) {
  NetStub *s;
  const char *u = url ? url : "";
  const char *b = body ? body : "";
  if (!g_net_fake)
    sz_testrt_net_install();
  for (s = g_stubs; s; s = s->next) {
    if (strcmp(s->url, u) == 0) {
      sz_free(s->body);
      s->body = (char *)sz_alloc(strlen(b) + 1);
      memcpy(s->body, b, strlen(b) + 1);
      return;
    }
  }
  s = (NetStub *)sz_alloc(sizeof(NetStub));
  s->url = (char *)sz_alloc(strlen(u) + 1);
  memcpy(s->url, u, strlen(u) + 1);
  s->body = (char *)sz_alloc(strlen(b) + 1);
  memcpy(s->body, b, strlen(b) + 1);
  s->next = g_stubs;
  g_stubs = s;
}

static void *stub_http_get(void *env) {
  SzString *url_s = (SzString *)env;
  BoxResult *r = (BoxResult *)rc_box_zero(sizeof(BoxResult));
  const char *url = sz_string_cstr(url_s);
  NetStub *s;
  sz_timeline_log_cstr("Net.httpGet", url);
  if (sz_testrt_fault_tick(SZ_FAULT_NET)) {
    int mode = sz_testrt_fault_mode();
    if (mode == SZ_FAULT_CORRUPT) {
      for (s = g_stubs; s; s = s->next) {
        if (strcmp(s->url, url) == 0) {
          size_t n = strlen(s->body);
          char *bad = (char *)sz_alloc(n + 2);
          if (n)
            memcpy(bad, s->body, n);
          bad[n] = '!';
          bad[n + 1] = '\0';
          r->is_err = 0;
          r->as.ok = sz_string_from_cstr(bad);
          sz_free(bad);
          goto done;
        }
      }
    }
    r->is_err = 1;
    r->as.err = sz_error_new(
        6, mode == SZ_FAULT_DROP ? "Net.httpGet: stub response dropped"
                                 : "Net.httpGet: injected fault");
    goto done;
  }
  for (s = g_stubs; s; s = s->next) {
    if (strcmp(s->url, url) == 0) {
      r->is_err = 0;
      r->as.ok = sz_string_from_cstr(s->body);
      goto done;
    }
  }
  r->is_err = 1;
  r->as.err = sz_error_new(6, "Net.httpGet: no stub for URL");
done:
  return r;
}

SzIo *sz_testrt_net_http_get(SzString *url) {
  return fm_drop(sz_io_delay(stub_http_get, url), unwrap_box, NULL);
}

const char *sz_testrt_net_last_serve_body(void) {
  return g_last_serve_body ? g_last_serve_body : "";
}

void sz_testrt_net_set_last_serve_body(const char *body) {
  size_t n;
  sz_free(g_last_serve_body);
  g_last_serve_body = NULL;
  if (!body)
    return;
  n = strlen(body);
  g_last_serve_body = (char *)sz_alloc(n + 1);
  memcpy(g_last_serve_body, body, n + 1);
}

/* --- sys / console ------------------------------------------------------- */

static int g_sys_fake = 0;
static int g_args_override = 0;
static char **g_fake_argv = NULL;
static int g_fake_argc = 0;

static char *g_stdin_buf = NULL;
static size_t g_stdin_len = 0;
static size_t g_stdin_off = 0;

static char *g_stdout_buf = NULL;
static size_t g_stdout_len = 0;
static size_t g_stdout_cap = 0;

#define ENV_CAP 32
static char *g_env_keys[ENV_CAP];
static char *g_env_vals[ENV_CAP];
static int g_env_n;

#define PROC_CAP 32
static int64_t g_proc[PROC_CAP];
static int g_proc_n;

static void env_free_all(void) {
  int i;
  for (i = 0; i < g_env_n; i++) {
    sz_free(g_env_keys[i]);
    sz_free(g_env_vals[i]);
    g_env_keys[i] = NULL;
    g_env_vals[i] = NULL;
  }
  g_env_n = 0;
}

static char *env_dup(const char *s) {
  size_t n;
  char *d;
  if (!s)
    s = "";
  n = strlen(s);
  d = (char *)sz_alloc(n + 1);
  memcpy(d, s, n + 1);
  return d;
}

void sz_testrt_env_set(const char *key, const char *val) {
  int i;
  if (!key || !key[0])
    return;
  if (!val)
    val = "";
  for (i = 0; i < g_env_n; i++) {
    if (strcmp(g_env_keys[i], key) == 0) {
      sz_free(g_env_vals[i]);
      g_env_vals[i] = env_dup(val);
      return;
    }
  }
  if (g_env_n >= ENV_CAP)
    return;
  g_env_keys[g_env_n] = env_dup(key);
  g_env_vals[g_env_n] = env_dup(val);
  g_env_n++;
}

const char *sz_testrt_env_get(const char *key) {
  int i;
  if (!key)
    return NULL;
  for (i = 0; i < g_env_n; i++) {
    if (strcmp(g_env_keys[i], key) == 0)
      return g_env_vals[i];
  }
  return NULL;
}

static void proc_clear(void) { g_proc_n = 0; }

void sz_testrt_proc_put(int64_t pid) {
  int i;
  if (pid <= 0)
    return;
  for (i = 0; i < g_proc_n; i++) {
    if (g_proc[i] == pid)
      return;
  }
  if (g_proc_n >= PROC_CAP)
    return;
  g_proc[g_proc_n++] = pid;
}

int sz_testrt_proc_alive(int64_t pid) {
  int i;
  if (pid <= 0)
    return 0;
  for (i = 0; i < g_proc_n; i++) {
    if (g_proc[i] == pid)
      return 1;
  }
  return 0;
}

void sz_testrt_proc_kill(int64_t pid) {
  int i;
  for (i = 0; i < g_proc_n; i++) {
    if (g_proc[i] == pid) {
      g_proc[i] = g_proc[g_proc_n - 1];
      g_proc_n--;
      return;
    }
  }
}

static void env_copy_host(const char *key) {
  const char *v = getenv(key);
  if (v && v[0])
    sz_testrt_env_set(key, v);
}

static void free_fake_argv(void) {
  int i;
  if (!g_fake_argv)
    return;
  for (i = 0; i < g_fake_argc; i++)
    sz_free(g_fake_argv[i]);
  sz_free(g_fake_argv);
  g_fake_argv = NULL;
  g_fake_argc = 0;
  g_args_override = 0;
}

static void sz_testrt_sys_reset_live(void) {
  free_fake_argv();
  sz_free(g_stdin_buf);
  g_stdin_buf = NULL;
  g_stdin_len = 0;
  g_stdin_off = 0;
  sz_free(g_stdout_buf);
  g_stdout_buf = NULL;
  g_stdout_len = 0;
  g_stdout_cap = 0;
  env_free_all();
  proc_clear();
  g_sys_fake = 0;
}

void sz_testrt_stdout_reset(void) {
  if (g_stdout_buf)
    g_stdout_buf[0] = '\0';
  g_stdout_len = 0;
}

void sz_testrt_stdout_write(const char *bytes, size_t n) {
  size_t need;
  if (!bytes)
    bytes = "";
  need = g_stdout_len + n + 1; /* bytes + NUL */
  if (need > g_stdout_cap) {
    size_t cap = g_stdout_cap ? g_stdout_cap : 64;
    char *nb;
    while (cap < need)
      cap *= 2;
    nb = (char *)sz_alloc(cap);
    if (g_stdout_buf && g_stdout_len)
      memcpy(nb, g_stdout_buf, g_stdout_len);
    sz_free(g_stdout_buf);
    g_stdout_buf = nb;
    g_stdout_cap = cap;
  }
  if (n)
    memcpy(g_stdout_buf + g_stdout_len, bytes, n);
  g_stdout_len += n;
  g_stdout_buf[g_stdout_len] = '\0';
}

void sz_testrt_stdout_append(const char *line) {
  if (!line)
    line = "";
  sz_testrt_stdout_write(line, strlen(line));
  sz_testrt_stdout_write("\n", 1);
}

const char *sz_testrt_stdout_cstr(void) {
  return g_stdout_buf ? g_stdout_buf : "";
}

void sz_testrt_stdin_feed(const char *text) {
  size_t n;
  if (!text)
    text = "";
  n = strlen(text);
  sz_free(g_stdin_buf);
  g_stdin_buf = (char *)sz_alloc(n + 1);
  memcpy(g_stdin_buf, text, n + 1);
  g_stdin_len = n;
  g_stdin_off = 0;
}

void sz_testrt_sys_set_args(int argc, char **argv) {
  int i;
  free_fake_argv();
  if (argc < 0)
    argc = 0;
  g_fake_argc = argc;
  g_fake_argv = (char **)sz_alloc(sizeof(char *) * (size_t)(argc + 1));
  for (i = 0; i < argc; i++) {
    const char *s = argv && argv[i] ? argv[i] : "";
    size_t n = strlen(s);
    g_fake_argv[i] = (char *)sz_alloc(n + 1);
    memcpy(g_fake_argv[i], s, n + 1);
  }
  g_fake_argv[argc] = NULL;
  g_args_override = 1;
}

int sz_testrt_sys_has_args_override(void) { return g_args_override; }

SzList *sz_testrt_sys_args_list(void) {
  SzList *acc = sz_list_nil();
  int i;
  /* Override stores user args only (no argv[0]). */
  for (i = g_fake_argc - 1; i >= 0; i--) {
    SzString *s = sz_string_from_cstr(g_fake_argv[i] ? g_fake_argv[i] : "");
    SzList *old = acc;
    acc = sz_list_cons(s, old);
    sz_release(s);
    sz_release(old);
  }
  return acc;
}

static void *testrt_read_line_thunk(void *env) {
  size_t start;
  size_t end;
  (void)env;
  if (!g_stdin_buf || g_stdin_off >= g_stdin_len)
    return sz_string_from_cstr("");
  start = g_stdin_off;
  end = start;
  while (end < g_stdin_len && g_stdin_buf[end] != '\n')
    end++;
  g_stdin_off = end < g_stdin_len ? end + 1 : end;
  /* Strip trailing CR for CRLF. */
  if (end > start && g_stdin_buf[end - 1] == '\r')
    end--;
  return sz_string_from_bytes(g_stdin_buf + start, end - start);
}

SzIo *sz_testrt_sys_read_line(void) {
  return sz_io_delay(testrt_read_line_thunk, NULL);
}

static void *testrt_read_n_thunk(void *env) {
  int64_t n = sz_unbox_i64(env);
  size_t want;
  size_t avail;
  size_t take;
  void *s;
  if (n <= 0)
    return sz_string_from_cstr("");
  if (!g_stdin_buf || g_stdin_off >= g_stdin_len)
    return sz_string_from_cstr("");
  want = (size_t)n;
  avail = g_stdin_len - g_stdin_off;
  take = want < avail ? want : avail;
  s = sz_string_from_bytes(g_stdin_buf + g_stdin_off, take);
  g_stdin_off += take;
  return s;
}

SzIo *sz_testrt_sys_read(int64_t n) {
  void *b = sz_box_i64(n);
  SzIo *io = sz_io_delay(testrt_read_n_thunk, b);
  sz_release(b);
  return io;
}

static void sz_testrt_sys_install(void) {
  sz_testrt_sys_reset_live();
  g_sys_fake = 1;
  /* App fixtures the CLI sets. Do not copy PATH/HOME/SCUZZ_TESTRT. */
  env_copy_host("SCUZZ_TODO_PATH");
  env_copy_host("SCUZZ_SERVE");
  env_copy_host("SCUZZ_KIT");
}

int sz_testrt_sys_is_fake(void) { return g_sys_fake; }

/* --- install / reset ----------------------------------------------------- */

void sz_testrt_install(void) {
  fault_arm_from_env();
  sz_testrt_clock_install(1);
  sz_testrt_random_install(42);
  sz_testrt_fs_install();
  sz_testrt_net_install();
  sz_testrt_sys_install();
}

void sz_testrt_reset(void) {
  g_fault_kind = 0;
  g_fault_mode = SZ_FAULT_FAIL;
  g_fault_n = 0;
  g_fault_seed = 0;
  g_fault_count_fs = 0;
  g_fault_count_net = 0;
  g_fault_count_queue = 0;
  g_fault_fail_msg = NULL;
  g_idle_have = 0;
  g_idle_bytes = 0;
  g_idle_count = 0;
  g_heap_have = 0;
  g_sess_have = 0;
  g_skip_orphan_cancel = 0;
  g_skip_unstepped_ensure = 0;
  sz_property_session_reset();
  sz_testrt_clock_reset_live();
  sz_testrt_random_reset_live();
  sz_testrt_fs_reset_live();
  sz_testrt_net_reset_live();
  sz_testrt_sys_reset_live();
}

/* --- residual properties (armed under SCUZZ_TESTRT=1) -------------------------- */

static char *g_property_a11y = NULL;
static char *g_property_last_hit = NULL;
static int g_replay;
static const char *g_replay_a11y;
static const char *g_replay_last_hit;
static const char *g_replay_signals;

void sz_property_stash_a11y(const char *dump) {
  size_t n;
  char *copy;
  if (g_property_a11y) {
    free(g_property_a11y);
    g_property_a11y = NULL;
  }
  if (!dump)
    dump = "";
  n = strlen(dump);
  copy = (char *)malloc(n + 1);
  if (!copy)
    sz_panic("timeline: out of memory");
  memcpy(copy, dump, n + 1);
  g_property_a11y = copy;
}

int64_t sz_property_a11y_has(SzString *needle) {
  const char *n = needle ? sz_string_cstr(needle) : "";
  const char *hay = g_replay ? g_replay_a11y : g_property_a11y;
  if (!hay || !n[0])
    return 0;
  return strstr(hay, n) != NULL ? 1 : 0;
}

void sz_property_stash_last_hit(const char *desc) {
  size_t n;
  char *copy;
  if (g_property_last_hit) {
    free(g_property_last_hit);
    g_property_last_hit = NULL;
  }
  if (!desc)
    desc = "";
  n = strlen(desc);
  copy = (char *)malloc(n + 1);
  if (!copy)
    sz_panic("timeline: out of memory");
  memcpy(copy, desc, n + 1);
  g_property_last_hit = copy;
}

int64_t sz_property_last_hit_has(SzString *needle) {
  const char *n = needle ? sz_string_cstr(needle) : "";
  const char *hay = g_replay ? g_replay_last_hit : g_property_last_hit;
  if (!hay || !n[0])
    return 0;
  return strstr(hay, n) != NULL ? 1 : 0;
}

SzIo *sz_property_assert(SzString *name, int64_t ok) {
  const char *tr = getenv("SCUZZ_TESTRT");
  char buf[256];
  if (!tr || tr[0] != '1')
    return pure_drop(NULL);
  if (ok)
    return pure_drop(NULL);
  snprintf(buf, sizeof buf, "property failed: %s", name ? sz_string_cstr(name) : "?");
  fprintf(stderr, "scuzz: %s\n", buf);
  return sz_io_fail_cstr(buf);
}

void sz_property_check(SzString *name, int64_t ok) {
  const char *tr = getenv("SCUZZ_TESTRT");
  char buf[256];
  if (!tr || tr[0] != '1')
    return;
  if (ok)
    return;
  snprintf(buf, sizeof buf, "property check failed: %s",
           name ? sz_string_cstr(name) : "?");
  fprintf(stderr, "scuzz: %s\n", buf);
  sz_panic(buf);
}

#define SZ_SOMETIMES_MAX 64
static char *g_sometimes[SZ_SOMETIMES_MAX];
static int g_sometimes_n;

static void sometimes_record(const char *s) {
  int i;
  size_t n;
  char *copy;
  for (i = 0; i < g_sometimes_n; i++) {
    if (strcmp(g_sometimes[i], s) == 0)
      return;
  }
  if (g_sometimes_n >= SZ_SOMETIMES_MAX)
    return;
  n = strlen(s);
  copy = (char *)malloc(n + 1);
  if (!copy)
    sz_panic("timeline: out of memory");
  memcpy(copy, s, n + 1);
  g_sometimes[g_sometimes_n++] = copy;
  /* Reachability names stay live. Do not treat that retain as a UI leak. */
  sz_testrt_ui_idle_reset();
}

void sz_property_sometimes(SzString *name) {
  const char *tr = getenv("SCUZZ_TESTRT");
  const char *s;
  if (!tr || tr[0] != '1')
    return;
  s = name ? sz_string_cstr(name) : "";
  if (!s[0])
    return;
  sometimes_record(s);
}

void sz_property_sometimes_flush(void) {
  const char *path = getenv("SCUZZ_SOMETIMES_DUMP");
  FILE *f;
  int i;
  if (!path || !path[0])
    return;
  f = fopen(path, "w");
  if (!f)
    return;
  for (i = 0; i < g_sometimes_n; i++)
    fprintf(f, "%s\n", g_sometimes[i]);
  fclose(f);
}

#define SZ_CLASSIFY_MAX 64
typedef struct {
  char *name;
  int yes;
  int no;
} SzClassify;

static SzClassify g_classify[SZ_CLASSIFY_MAX];
static int g_classify_n;

void sz_property_classify(SzString *name, int64_t hit) {
  const char *tr = getenv("SCUZZ_TESTRT");
  const char *s;
  int i;
  size_t n;
  char *copy;
  if (!tr || tr[0] != '1')
    return;
  s = name ? sz_string_cstr(name) : "";
  if (!s[0])
    return;
  for (i = 0; i < g_classify_n; i++) {
    if (strcmp(g_classify[i].name, s) == 0) {
      if (hit)
        g_classify[i].yes++;
      else
        g_classify[i].no++;
      sz_testrt_ui_idle_reset();
      return;
    }
  }
  if (g_classify_n >= SZ_CLASSIFY_MAX)
    return;
  n = strlen(s);
  copy = (char *)malloc(n + 1);
  if (!copy)
    sz_panic("timeline: out of memory");
  memcpy(copy, s, n + 1);
  g_classify[g_classify_n].name = copy;
  g_classify[g_classify_n].yes = hit ? 1 : 0;
  g_classify[g_classify_n].no = hit ? 0 : 1;
  g_classify_n++;
  sz_testrt_ui_idle_reset();
}

void sz_property_classify_flush(void) {
  const char *path = getenv("SCUZZ_CLASSIFY_DUMP");
  FILE *f;
  int i;
  if (!path || !path[0])
    return;
  f = fopen(path, "w");
  if (!f)
    return;
  for (i = 0; i < g_classify_n; i++)
    fprintf(f, "%s %d %d\n", g_classify[i].name, g_classify[i].yes,
            g_classify[i].no);
  fclose(f);
}

#define SZ_SESSION_MAX 32
typedef struct {
  char *name;
  int64_t (*fn)(void);
  int held;
} SzSessionProp;

static SzSessionProp g_always[SZ_SESSION_MAX];
static int g_always_n;
static SzSessionProp g_eventually[SZ_SESSION_MAX];
static int g_eventually_n;
typedef struct {
  char *name;
  int64_t (*trigger)(void);
  int64_t (*response)(void);
} SzResponseProp;

static SzResponseProp g_response[SZ_SESSION_MAX];
static int g_response_n;

typedef struct {
  char *name;
  SzVerdict *(*fn)(void *);
} SzVerifyProp;

static SzVerifyProp g_verify[SZ_SESSION_MAX];
static int g_verify_n;

typedef struct {
  char *name;
  SzVerdict *(*fn)(void *, void *);
} SzVerifyRel;

static SzVerifyRel g_verify_rel[SZ_SESSION_MAX];
static int g_verify_rel_n;

typedef struct {
  char *signals;
  char *a11y;
  char *last_hit;
  char *drive;
  char *effects;
  char *fibers;
  char *fault;
  int checkpoint;
} SzTlState;

static SzTlState *g_tl;
static int g_tl_n;
static int g_tl_cap;
static int g_tl_warned;
static char *g_last_drive;

typedef struct {
  int n;
  int fail_index;
  SzTlState *states;
} SzTimeline;

#define SZ_EFFECT_CAP 65536
static char g_effects[SZ_EFFECT_CAP];
static size_t g_effects_len;

typedef struct TlIntern {
  char *s;
  int rc;
  struct TlIntern *next;
} TlIntern;

static TlIntern *g_intern;

static char *tl_copy(const char *s) {
  size_t n;
  char *c;
  if (!s)
    s = "";
  n = strlen(s);
  c = (char *)malloc(n + 1);
  if (!c)
    sz_panic("timeline: out of memory");
  memcpy(c, s, n + 1);
  return c;
}

/* Intern identical observation strings so consecutive equal dumps share
 * one copy. tl_at is the algebra seam: claims never index g_tl directly. */
static char *tl_intern(const char *s) {
  TlIntern *p;
  size_t n;
  if (!s)
    s = "";
  for (p = g_intern; p; p = p->next) {
    if (strcmp(p->s, s) == 0) {
      p->rc += 1;
      return p->s;
    }
  }
  n = strlen(s);
  p = (TlIntern *)malloc(sizeof(TlIntern));
  if (!p)
    sz_panic("timeline: out of memory");
  p->s = (char *)malloc(n + 1);
  if (!p->s)
    sz_panic("timeline: out of memory");
  memcpy(p->s, s, n + 1);
  p->rc = 1;
  p->next = g_intern;
  g_intern = p;
  return p->s;
}

static void tl_intern_drop(char *s) {
  TlIntern **pp;
  if (!s)
    return;
  for (pp = &g_intern; *pp; pp = &(*pp)->next) {
    if ((*pp)->s != s)
      continue;
    (*pp)->rc -= 1;
    if ((*pp)->rc <= 0) {
      TlIntern *gone = *pp;
      *pp = gone->next;
      free(gone->s);
      free(gone);
    }
    return;
  }
}

static void tl_intern_drop_state(SzTlState *s) {
  tl_intern_drop(s->signals);
  tl_intern_drop(s->a11y);
  tl_intern_drop(s->last_hit);
  tl_intern_drop(s->drive);
  tl_intern_drop(s->effects);
  tl_intern_drop(s->fibers);
  tl_intern_drop(s->fault);
}

static uint64_t tl_hash_bytes(const void *p, size_t n) {
  const unsigned char *b = (const unsigned char *)p;
  uint64_t h = 1469598103934665603ULL;
  size_t i;
  for (i = 0; i < n; i++) {
    h ^= b[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static int tl_log_armed(void) {
  const char *tr = getenv("SCUZZ_TESTRT");
  const char *dump = getenv("SCUZZ_TIMELINE_DUMP");
  return (tr && tr[0] == '1') || (dump && dump[0]);
}

void sz_timeline_log_bytes(const char *op, const void *p, size_t n) {
  char line[160];
  int m;
  uint64_t h;
  if (!tl_log_armed() || !op || !op[0])
    return;
  h = n ? tl_hash_bytes(p, n) : 0;
  m = snprintf(line, sizeof line, "%s bytes=%lld hash=%016llx\n", op,
               (long long)n, (unsigned long long)h);
  if (m < 0)
    return;
  if ((size_t)m >= sizeof line)
    m = (int)sizeof line - 1;
  if (g_effects_len + (size_t)m >= SZ_EFFECT_CAP)
    return;
  memcpy(g_effects + g_effects_len, line, (size_t)m);
  g_effects_len += (size_t)m;
  g_effects[g_effects_len] = 0;
}

void sz_timeline_log_cstr(const char *op, const char *s) {
  const char *p = s ? s : "";
  sz_timeline_log_bytes(op, p, strlen(p));
}

static const char *tl_fault_kind_name(int k) {
  if (k == SZ_FAULT_FS)
    return "fs";
  if (k == SZ_FAULT_NET)
    return "net";
  if (k == SZ_FAULT_QUEUE)
    return "queue";
  return "none";
}

static const char *tl_fault_mode_name(int m) {
  if (m == SZ_FAULT_DROP)
    return "drop";
  if (m == SZ_FAULT_CORRUPT)
    return "corrupt";
  return "fail";
}

static void tl_fmt_fibers(char *buf, size_t cap) {
  int64_t live = 0;
  int64_t ready = 0;
  int64_t parked = 0;
  int64_t done = 0;
  sz_fiber_census(&live, &ready, &parked, &done);
  snprintf(buf, cap, "live=%lld ready=%lld parked=%lld done=%lld",
           (long long)live, (long long)ready, (long long)parked,
           (long long)done);
}

static void tl_fmt_fault(char *buf, size_t cap) {
  snprintf(buf, cap, "kind=%s n=%d mode=%s seed=%d",
           tl_fault_kind_name(g_fault_kind), g_fault_n,
           tl_fault_mode_name(g_fault_mode), g_fault_seed);
}

static int tl_checkpoint_interval(void) {
  const char *s = getenv("SCUZZ_TIMELINE_CHECKPOINT");
  int n;
  if (!s || !s[0])
    return 0;
  n = atoi(s);
  return n < 0 ? 0 : n;
}

static void tl_effects_reset(void) {
  g_effects_len = 0;
  g_effects[0] = 0;
}

static void tl_free_states(void) {
  int i;
  for (i = 0; i < g_tl_n; i++)
    tl_intern_drop_state(&g_tl[i]);
  free(g_tl);
  g_tl = NULL;
  g_tl_n = 0;
  g_tl_cap = 0;
  g_tl_warned = 0;
  tl_effects_reset();
}

void sz_timeline_set_drive(const char *line) {
  free(g_last_drive);
  g_last_drive = tl_copy(line ? line : "");
}

static void tl_push(void) {
  SzString *dump;
  char fibers[96];
  char fault[96];
  int step;
  if (g_tl_n >= 1024) {
    if (!g_tl_warned) {
      fprintf(stderr, "scuzz: timeline capped at 1024 states\n");
      g_tl_warned = 1;
    }
    return;
  }
  if (g_tl_n >= g_tl_cap) {
    int ncap = g_tl_cap ? g_tl_cap * 2 : 16;
    SzTlState *nb = (SzTlState *)malloc((size_t)ncap * sizeof(SzTlState));
    if (!nb)
      sz_panic("timeline: out of memory");
    if (g_tl) {
      memcpy(nb, g_tl, (size_t)g_tl_n * sizeof(SzTlState));
      free(g_tl);
    }
    g_tl = nb;
    g_tl_cap = ncap;
  }
  dump = sz_signal_dump();
  g_tl[g_tl_n].signals = tl_intern(dump ? sz_string_cstr(dump) : "");
  if (dump)
    sz_string_free(dump);
  g_tl[g_tl_n].a11y = tl_intern(g_property_a11y);
  g_tl[g_tl_n].last_hit = tl_intern(g_property_last_hit);
  g_tl[g_tl_n].drive = tl_intern(g_last_drive);
  g_tl[g_tl_n].effects = tl_intern(g_effects);
  tl_fmt_fibers(fibers, sizeof fibers);
  tl_fmt_fault(fault, sizeof fault);
  g_tl[g_tl_n].fibers = tl_intern(fibers);
  g_tl[g_tl_n].fault = tl_intern(fault);
  step = tl_checkpoint_interval();
  g_tl[g_tl_n].checkpoint = (step <= 0) || (g_tl_n % step == 0);
  g_tl_n++;
  tl_effects_reset();
}

static void tl_restore(int i) {
  if (i < 0 || i >= g_tl_n) {
    g_replay = 0;
    return;
  }
  g_replay = 1;
  g_replay_a11y = g_tl[i].a11y;
  g_replay_last_hit = g_tl[i].last_hit;
  g_replay_signals = g_tl[i].signals;
}

static void tl_restore_clear(void) {
  g_replay = 0;
  g_replay_a11y = NULL;
  g_replay_last_hit = NULL;
  g_replay_signals = NULL;
}

int sz_timeline_replaying(void) { return g_replay; }

int64_t sz_timeline_replay_signal_int(int64_t id) {
  char key[48];
  const char *p;
  if (!g_replay_signals)
    return 0;
  snprintf(key, sizeof key, "int[%lld] = ", (long long)id);
  p = strstr(g_replay_signals, key);
  if (!p)
    return 0;
  return (int64_t)atoll(p + strlen(key));
}

static int64_t tl_parse_signal_int(const char *dump, int64_t id) {
  char key[48];
  const char *p;
  if (!dump)
    return 0;
  snprintf(key, sizeof key, "int[%lld] = ", (long long)id);
  p = strstr(dump, key);
  if (!p)
    return 0;
  return (int64_t)atoll(p + strlen(key));
}

static SzTlState *tl_at(void *tl, int64_t i) {
  SzTimeline *t = (SzTimeline *)tl;
  if (!t || i < 0 || i >= t->n)
    return NULL;
  return &t->states[i];
}

int64_t sz_timeline_len(void *tl) {
  SzTimeline *t = (SzTimeline *)tl;
  return t ? t->n : 0;
}

int64_t sz_timeline_signal_int(void *tl, int64_t i, int64_t id) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_signal_int(s->signals, id) : 0;
}

/* List length from the signals dump: `list[<id>] = ["a", "b"]` holds one
 * quoted string per element (no escaping), so quotes pair per element. */
static int64_t tl_parse_signal_list_len(const char *dump, int64_t id) {
  char key[48];
  const char *p;
  int64_t quotes = 0;
  if (!dump)
    return 0;
  snprintf(key, sizeof key, "list[%lld] = [", (long long)id);
  p = strstr(dump, key);
  if (!p)
    return 0;
  p += strlen(key);
  while (*p && *p != ']') {
    if (*p == '"')
      quotes += 1;
    p += 1;
  }
  return quotes / 2;
}

int64_t sz_timeline_signal_list_len(void *tl, int64_t i, int64_t id) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_signal_list_len(s->signals, id) : 0;
}

/* 1 when the `str[<id>] = "<value>"` line in the state's signals dump holds
 * `needle` as a substring. */
int64_t sz_timeline_signal_str_has(void *tl, int64_t i, int64_t id,
                                   SzString *needle) {
  char key[48];
  const char *p;
  const char *end;
  SzTlState *s = tl_at(tl, i);
  const char *n = needle ? sz_string_cstr(needle) : "";
  if (!s || !s->signals || !n[0])
    return 0;
  snprintf(key, sizeof key, "str[%lld]", (long long)id);
  p = strstr(s->signals, key);
  if (!p)
    return 0;
  end = strchr(p, '\n');
  if (!end)
    end = p + strlen(p);
  {
    size_t len = (size_t)(end - p);
    size_t m = strlen(n);
    size_t k;
    if (m > len)
      return 0;
    for (k = 0; k + m <= len; k++) {
      if (memcmp(p + k, n, m) == 0)
        return 1;
    }
  }
  return 0;
}

int64_t sz_timeline_a11y_has(void *tl, int64_t i, SzString *needle) {
  SzTlState *s = tl_at(tl, i);
  const char *n = needle ? sz_string_cstr(needle) : "";
  if (!s || !s->a11y || !n[0])
    return 0;
  return strstr(s->a11y, n) != NULL ? 1 : 0;
}

int64_t sz_timeline_last_hit_has(void *tl, int64_t i, SzString *needle) {
  SzTlState *s = tl_at(tl, i);
  const char *n = needle ? sz_string_cstr(needle) : "";
  if (!s || !s->last_hit || !n[0])
    return 0;
  return strstr(s->last_hit, n) != NULL ? 1 : 0;
}

static int64_t tl_field_has(const char *field, SzString *needle) {
  const char *n = needle ? sz_string_cstr(needle) : "";
  if (!field || !n[0])
    return 0;
  return strstr(field, n) != NULL ? 1 : 0;
}

int64_t sz_timeline_drive_has(void *tl, int64_t i, SzString *needle) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_field_has(s->drive, needle) : 0;
}

int64_t sz_timeline_effect_has(void *tl, int64_t i, SzString *needle) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_field_has(s->effects, needle) : 0;
}

int64_t sz_timeline_effect_count(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  const char *p;
  int64_t n = 0;
  if (!s || !s->effects || !s->effects[0])
    return 0;
  p = s->effects;
  while (*p) {
    if (*p == '\n')
      n += 1;
    p += 1;
  }
  if (s->effects[0] && s->effects[strlen(s->effects) - 1] != '\n')
    n += 1;
  return n;
}

static int64_t tl_parse_labeled(const char *dump, const char *key) {
  const char *p;
  if (!dump || !key)
    return 0;
  p = strstr(dump, key);
  if (!p)
    return 0;
  return (int64_t)atoll(p + strlen(key));
}

int64_t sz_timeline_fiber_live(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_labeled(s->fibers, "live=") : 0;
}

int64_t sz_timeline_fiber_ready(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_labeled(s->fibers, "ready=") : 0;
}

int64_t sz_timeline_fiber_parked(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_labeled(s->fibers, "parked=") : 0;
}

int64_t sz_timeline_fiber_done(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_labeled(s->fibers, "done=") : 0;
}

int64_t sz_timeline_fault_kind_has(void *tl, int64_t i, SzString *needle) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_field_has(s->fault, needle) : 0;
}

int64_t sz_timeline_fault_n(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s ? tl_parse_labeled(s->fault, "n=") : 0;
}

int64_t sz_timeline_checkpoint(void *tl, int64_t i) {
  SzTlState *s = tl_at(tl, i);
  return s && s->checkpoint ? 1 : 0;
}

int64_t sz_timeline_nearest_checkpoint(void *tl, int64_t i) {
  SzTimeline *t = (SzTimeline *)tl;
  int j;
  int n;
  if (!t || t->n <= 0)
    return -1;
  n = t->n;
  if (i >= n)
    i = n - 1;
  if (i < 0)
    i = 0;
  for (j = (int)i; j >= 0; j--) {
    if (t->states[j].checkpoint)
      return j;
  }
  for (j = (int)i; j < n; j++) {
    if (t->states[j].checkpoint)
      return j;
  }
  return -1;
}

static void tl_compact_states(SzTlState *states, int *n) {
  int i;
  int w = 0;
  int cap = *n;
  for (i = 0; i < cap; i++) {
    if (!states[i].checkpoint) {
      tl_intern_drop_state(&states[i]);
      continue;
    }
    if (w != i)
      states[w] = states[i];
    w += 1;
  }
  *n = w;
}

void sz_timeline_compact(void) {
  int n = g_tl_n;
  if (!g_tl || n <= 0)
    return;
  tl_compact_states(g_tl, &n);
  g_tl_n = n;
}

void sz_timeline_compact_loaded(void *tl) {
  SzTimeline *t = (SzTimeline *)tl;
  int i;
  int w = 0;
  if (!t || t->n <= 0)
    return;
  for (i = 0; i < t->n; i++) {
    if (!t->states[i].checkpoint) {
      free(t->states[i].signals);
      free(t->states[i].a11y);
      free(t->states[i].last_hit);
      free(t->states[i].drive);
      free(t->states[i].effects);
      free(t->states[i].fibers);
      free(t->states[i].fault);
      continue;
    }
    if (w != i)
      t->states[w] = t->states[i];
    w += 1;
  }
  t->n = w;
}

void sz_timeline_replay_from(int64_t i) {
  int j;
  if (i < 0 || i >= g_tl_n) {
    tl_restore_clear();
    return;
  }
  for (j = (int)i; j >= 0; j--) {
    if (g_tl[j].checkpoint) {
      tl_restore(j);
      return;
    }
  }
  tl_restore((int)i);
}

int64_t sz_timeline_forall(void *tl, SzListPred pred, void *env) {
  SzTimeline *t = (SzTimeline *)tl;
  int i;
  if (!t || !pred)
    return 1;
  for (i = 0; i < t->n; i++) {
    void *box = sz_box_i64((int64_t)i);
    int64_t ok = pred(box, env);
    sz_release(box);
    if (!ok) {
      t->fail_index = i;
      return 0;
    }
  }
  return 1;
}

int64_t sz_timeline_exists(void *tl, SzListPred pred, void *env) {
  SzTimeline *t = (SzTimeline *)tl;
  int i;
  if (!t || !pred)
    return 0;
  for (i = 0; i < t->n; i++) {
    void *box = sz_box_i64((int64_t)i);
    int64_t ok = pred(box, env);
    sz_release(box);
    if (ok)
      return 1;
  }
  t->fail_index = t->n > 0 ? t->n - 1 : 0;
  return 0;
}
SzVerdict *sz_verdict_ok(void) {
  static SzVerdict ok = {1, -1, NULL};
  return &ok;
}

SzVerdict *sz_verdict_fail(int64_t index, const char *why) {
  /* Plain malloc: judge-side memory must not perturb app heap accounting. */
  SzVerdict *v = (SzVerdict *)malloc(sizeof(SzVerdict));
  if (!v)
    sz_panic("verdict: out of memory");
  v->valid = 0;
  v->index = index;
  v->why = why;
  return v;
}

SzVerdict *sz_verdict_and(SzVerdict *a, SzVerdict *b) {
  if (a && !a->valid)
    return a;
  return b;
}

SzVerdict *sz_verdict_or(SzVerdict *a, SzVerdict *b) {
  if (a && a->valid)
    return a;
  return b;
}

SzVerdict *sz_verdict_every(void *tl, void *fnp, void *envp) {
  SzTimeline *t = (SzTimeline *)tl;
  SzListPred pred = (SzListPred)fnp;
  int i;
  if (!t || !pred)
    return sz_verdict_ok();
  for (i = 0; i < t->n; i++) {
    void *box = sz_box_i64((int64_t)i);
    int64_t ok = pred(box, envp);
    sz_release(box);
    if (!ok)
      return sz_verdict_fail(i, "predicate false at this state");
  }
  return sz_verdict_ok();
}

SzVerdict *sz_verdict_any(void *tl, void *fnp, void *envp) {
  SzTimeline *t = (SzTimeline *)tl;
  SzListPred pred = (SzListPred)fnp;
  int i;
  if (t && pred) {
    for (i = 0; i < t->n; i++) {
      void *box = sz_box_i64((int64_t)i);
      int64_t ok = pred(box, envp);
      sz_release(box);
      if (ok)
        return sz_verdict_ok();
    }
  }
  return sz_verdict_fail(t && t->n > 0 ? t->n - 1 : 0,
                         "no state satisfied the predicate");
}

#define SZ_TIMELINE_DUMP_VERSION 2
#define SZ_TIMELINE_DUMP_VERSION_MIN 1

static void tl_fputs_block(FILE *f, const char *s) {
  fputs(s ? s : "", f);
  if (s && s[0] && s[strlen(s) - 1] != '\n')
    fputc('\n', f);
}

static void tl_dump_file(void) {
  const char *path = getenv("SCUZZ_TIMELINE_DUMP");
  FILE *f;
  int i;
  if (!path || !path[0])
    return;
  f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "# timeline v=%d n=%d\n", SZ_TIMELINE_DUMP_VERSION, g_tl_n);
  for (i = 0; i < g_tl_n; i++) {
    fprintf(f, "--- %d checkpoint=%d\n", i, g_tl[i].checkpoint ? 1 : 0);
    fprintf(f, "last_hit:\n%s\n", g_tl[i].last_hit ? g_tl[i].last_hit : "");
    fprintf(f, "drive:\n%s\n", g_tl[i].drive ? g_tl[i].drive : "");
    fprintf(f, "signals:\n");
    tl_fputs_block(f, g_tl[i].signals);
    fprintf(f, "a11y:\n");
    tl_fputs_block(f, g_tl[i].a11y);
    fprintf(f, "effects:\n");
    tl_fputs_block(f, g_tl[i].effects);
    fprintf(f, "fibers:\n%s\n", g_tl[i].fibers ? g_tl[i].fibers : "");
    fprintf(f, "fault:\n%s\n", g_tl[i].fault ? g_tl[i].fault : "");
  }
  fclose(f);
}

/* --- v1 dump loader + relation judge (SCUZZ_JUDGE_REL) --------------------- */

static void verdict_msg(char *buf, size_t cap, const char *name,
                        const SzVerdict *v);

static char *tl_read_all(const char *path) {
  FILE *f = fopen(path, "rb");
  long n;
  char *buf;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n < 0)
    n = 0;
  buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    sz_panic("timeline: out of memory");
  }
  n = (long)fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[n] = 0;
  return buf;
}

static char *tl_copy_n(const char *s, size_t n) {
  char *c = (char *)malloc(n + 1);
  if (!c)
    sz_panic("timeline: out of memory");
  memcpy(c, s, n);
  c[n] = 0;
  return c;
}

static void tl_cat(char **buf, size_t *len, size_t *cap, const char *s,
                   size_t n) {
  if (*len + n + 1 > *cap) {
    size_t ncap = *cap ? *cap : 64;
    char *nb;
    while (*len + n + 1 > ncap)
      ncap *= 2;
    nb = (char *)malloc(ncap);
    if (!nb)
      sz_panic("timeline: out of memory");
    if (*buf) {
      memcpy(nb, *buf, *len);
      free(*buf);
    }
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = 0;
}

typedef struct {
  const char *cur;
} SzTlScan;

/* Next line without its newline. 0 at EOF. */
static int tl_scan_line(SzTlScan *s, const char **start, size_t *len) {
  const char *nl;
  if (!s->cur || !*s->cur)
    return 0;
  nl = strchr(s->cur, '\n');
  if (!nl) {
    *start = s->cur;
    *len = strlen(s->cur);
    s->cur += *len;
    return 1;
  }
  *start = s->cur;
  *len = (size_t)(nl - s->cur);
  s->cur = nl + 1;
  return 1;
}

static int tl_scan_peek(SzTlScan *s, const char *prefix) {
  return s->cur && strncmp(s->cur, prefix, strlen(prefix)) == 0;
}

static int tl_expect(SzTlScan *scan, const char *want) {
  const char *line;
  size_t len;
  if (!tl_scan_line(scan, &line, &len))
    return 0;
  return len == strlen(want) && strncmp(line, want, len) == 0;
}

void sz_timeline_free(void *tl) {
  SzTimeline *t = (SzTimeline *)tl;
  int i;
  if (!t)
    return;
  for (i = 0; i < t->n; i++) {
    free(t->states[i].signals);
    free(t->states[i].a11y);
    free(t->states[i].last_hit);
    free(t->states[i].drive);
    free(t->states[i].effects);
    free(t->states[i].fibers);
    free(t->states[i].fault);
  }
  free(t->states);
  free(t);
}

static int tl_scan_block(SzTlScan *scan, const char *stop1, const char *stop2,
                         char **out) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  while (!tl_scan_peek(scan, stop1) &&
         !(stop2 && tl_scan_peek(scan, stop2))) {
    const char *line;
    size_t n;
    if (!tl_scan_line(scan, &line, &n))
      break;
    tl_cat(&buf, &len, &cap, line, n);
    tl_cat(&buf, &len, &cap, "\n", 1);
  }
  *out = buf;
  return 1;
}

/* Parse v1 or v2. Optional trailing effects/fibers/fault keep handwritten
 * v=1 dumps loadable. v=2 dumps always write those blocks. */
void *sz_timeline_load(const char *path) {
  char *buf = tl_read_all(path);
  SzTlScan scan;
  const char *line;
  size_t len;
  char hdr[64];
  int v = -1;
  int n = -1;
  int i;
  SzTimeline *t;
  if (!buf) {
    fprintf(stderr, "scuzz: judge: cannot read %s\n", path ? path : "?");
    return NULL;
  }
  scan.cur = buf;
  if (!tl_scan_line(&scan, &line, &len) || len >= sizeof hdr) {
    fprintf(stderr, "scuzz: judge: %s: missing timeline header\n", path);
    free(buf);
    return NULL;
  }
  memcpy(hdr, line, len);
  hdr[len] = 0;
  if (sscanf(hdr, "# timeline v=%d n=%d", &v, &n) != 2 ||
      v < SZ_TIMELINE_DUMP_VERSION_MIN || v > SZ_TIMELINE_DUMP_VERSION ||
      n < 0) {
    fprintf(stderr, "scuzz: judge: %s: expected timeline v=%d dump\n", path,
            SZ_TIMELINE_DUMP_VERSION);
    free(buf);
    return NULL;
  }
  t = (SzTimeline *)malloc(sizeof(SzTimeline));
  if (!t)
    sz_panic("timeline: out of memory");
  t->n = n;
  t->fail_index = -1;
  t->states = (SzTlState *)calloc((size_t)(n > 0 ? n : 1), sizeof(SzTlState));
  if (!t->states)
    sz_panic("timeline: out of memory");
  for (i = 0; i < n; i++) {
    char *sig = NULL;
    char *a11y = NULL;
    size_t alen = 0, acap = 0;
    int ck = 1;
    int idx = -1;
    if (!tl_scan_line(&scan, &line, &len) || len < 4 ||
        strncmp(line, "--- ", 4) != 0)
      goto malformed;
    if (sscanf(line, "--- %d checkpoint=%d", &idx, &ck) != 2) {
      ck = 1;
      sscanf(line, "--- %d", &idx);
    }
    (void)idx;
    t->states[i].checkpoint = ck ? 1 : 0;
    if (!tl_expect(&scan, "last_hit:"))
      goto malformed;
    if (!tl_scan_line(&scan, &line, &len))
      goto malformed;
    t->states[i].last_hit = tl_copy_n(line, len);
    if (!tl_expect(&scan, "drive:"))
      goto malformed;
    if (!tl_scan_line(&scan, &line, &len))
      goto malformed;
    t->states[i].drive = tl_copy_n(line, len);
    if (!tl_expect(&scan, "signals:"))
      goto malformed;
    if (!tl_scan_block(&scan, "a11y:", NULL, &sig))
      goto malformed;
    if (!tl_expect(&scan, "a11y:"))
      goto malformed_sig;
    while (!tl_scan_peek(&scan, "--- ") && !tl_scan_peek(&scan, "effects:") &&
           !tl_scan_peek(&scan, "fibers:") && !tl_scan_peek(&scan, "fault:") &&
           tl_scan_line(&scan, &line, &len)) {
      tl_cat(&a11y, &alen, &acap, line, len);
      tl_cat(&a11y, &alen, &acap, "\n", 1);
    }
    t->states[i].signals = sig ? sig : tl_copy("");
    t->states[i].a11y = a11y ? a11y : tl_copy("");
    t->states[i].effects = tl_copy("");
    t->states[i].fibers = tl_copy("");
    t->states[i].fault = tl_copy("");
    if (tl_scan_peek(&scan, "effects:")) {
      char *eff = NULL;
      size_t elen = 0, ecap = 0;
      if (!tl_expect(&scan, "effects:"))
        goto malformed;
      while (!tl_scan_peek(&scan, "fibers:") && !tl_scan_peek(&scan, "fault:") &&
             !tl_scan_peek(&scan, "--- ") && tl_scan_line(&scan, &line, &len)) {
        tl_cat(&eff, &elen, &ecap, line, len);
        tl_cat(&eff, &elen, &ecap, "\n", 1);
      }
      free(t->states[i].effects);
      t->states[i].effects = eff ? eff : tl_copy("");
    }
    if (tl_scan_peek(&scan, "fibers:")) {
      char *fib = NULL;
      size_t flen = 0, fcap = 0;
      if (!tl_expect(&scan, "fibers:"))
        goto malformed;
      while (!tl_scan_peek(&scan, "fault:") && !tl_scan_peek(&scan, "--- ") &&
             tl_scan_line(&scan, &line, &len)) {
        tl_cat(&fib, &flen, &fcap, line, len);
        tl_cat(&fib, &flen, &fcap, "\n", 1);
      }
      free(t->states[i].fibers);
      t->states[i].fibers = fib ? fib : tl_copy("");
    }
    if (tl_scan_peek(&scan, "fault:")) {
      char *flt = NULL;
      size_t qlen = 0, qcap = 0;
      if (!tl_expect(&scan, "fault:"))
        goto malformed;
      while (!tl_scan_peek(&scan, "--- ") && tl_scan_line(&scan, &line, &len)) {
        tl_cat(&flt, &qlen, &qcap, line, len);
        tl_cat(&flt, &qlen, &qcap, "\n", 1);
      }
      free(t->states[i].fault);
      t->states[i].fault = flt ? flt : tl_copy("");
    }
    continue;
  malformed_sig:
    free(sig);
    free(a11y);
  malformed:
    fprintf(stderr, "scuzz: judge: %s: malformed dump at state %d\n", path, i);
    t->n = i;
    sz_timeline_free(t);
    free(buf);
    return NULL;
  }
  free(buf);
  return t;
}

int sz_judge_rel_main(const char *spec) {
  char *copy;
  char *comma;
  SzTimeline *a;
  SzTimeline *b;
  int i;
  int fails = 0;
  if (!spec)
    spec = "";
  copy = tl_copy(spec);
  comma = strchr(copy, ',');
  if (!comma) {
    fprintf(stderr, "scuzz: judge: SCUZZ_JUDGE_REL expects <a.txt>,<b.txt>\n");
    free(copy);
    return 2;
  }
  *comma = 0;
  a = (SzTimeline *)sz_timeline_load(copy);
  b = (SzTimeline *)sz_timeline_load(comma + 1);
  if (!a || !b) {
    sz_timeline_free(a);
    sz_timeline_free(b);
    free(copy);
    return 2;
  }
  if (g_verify_rel_n == 0)
    printf("scuzz: judge: no relation claims registered\n");
  for (i = 0; i < g_verify_rel_n; i++) {
    SzVerdict *v;
    char msg[256];
    if (!g_verify_rel[i].fn)
      continue;
    v = g_verify_rel[i].fn(a, b);
    if (!v || v->valid)
      continue;
    verdict_msg(msg, sizeof msg, g_verify_rel[i].name, v);
    fprintf(stderr, "scuzz: %s\n", msg);
    fails++;
  }
  sz_timeline_free(a);
  sz_timeline_free(b);
  free(copy);
  return fails ? 1 : 0;
}

static int tl_str_diff(const char *a, const char *b) {
  if (!a)
    a = "";
  if (!b)
    b = "";
  return strcmp(a, b) != 0;
}

void sz_timeline_varied_flush(void) {
  const char *path = getenv("SCUZZ_STATE_VARIED_DUMP");
  FILE *f;
  int i;
  int sig = 0;
  int a11y = 0;
  int hit = 0;
  int drive = 0;
  int effects = 0;
  int fibers = 0;
  int fault = 0;
  if (!path || !path[0])
    return;
  for (i = 1; i < g_tl_n; i++) {
    if (tl_str_diff(g_tl[i].signals, g_tl[0].signals))
      sig = 1;
    if (tl_str_diff(g_tl[i].a11y, g_tl[0].a11y))
      a11y = 1;
    if (tl_str_diff(g_tl[i].last_hit, g_tl[0].last_hit))
      hit = 1;
    if (tl_str_diff(g_tl[i].drive, g_tl[0].drive))
      drive = 1;
    if (tl_str_diff(g_tl[i].effects, g_tl[0].effects))
      effects = 1;
    if (tl_str_diff(g_tl[i].fibers, g_tl[0].fibers))
      fibers = 1;
    if (tl_str_diff(g_tl[i].fault, g_tl[0].fault))
      fault = 1;
  }
  f = fopen(path, "w");
  if (!f)
    return;
  if (sig)
    fputs("signals\n", f);
  if (a11y)
    fputs("a11y\n", f);
  if (hit)
    fputs("last_hit\n", f);
  if (drive)
    fputs("drive\n", f);
  if (effects)
    fputs("effects\n", f);
  if (fibers)
    fputs("fibers\n", f);
  if (fault)
    fputs("fault\n", f);
  fclose(f);
}

static void session_register(SzSessionProp *tab, int *n, SzString *name,
                             void *fn) {
  const char *s = name ? sz_string_cstr(name) : "";
  size_t len;
  char *copy;
  if (!fn || !s[0])
    return;
  if (*n >= SZ_SESSION_MAX)
    sz_panic("sz_property session: too many thunks");
  len = strlen(s);
  copy = (char *)sz_alloc(len + 1);
  memcpy(copy, s, len + 1);
  tab[*n].name = copy;
  tab[*n].fn = (int64_t(*)(void))fn;
  tab[*n].held = 0;
  *n += 1;
}

void sz_property_always_register(SzString *name, void *fn) {
  session_register(g_always, &g_always_n, name, fn);
}

void sz_property_eventually_register(SzString *name, void *fn) {
  session_register(g_eventually, &g_eventually_n, name, fn);
}
void sz_property_response_register(SzString *name, void *trigger_fn,
                                   void *response_fn) {
  const char *s = name ? sz_string_cstr(name) : "";
  size_t len;
  char *copy;
  if (!trigger_fn || !response_fn || !s[0])
    return;
  if (g_response_n >= SZ_SESSION_MAX)
    sz_panic("sz_property session: too many thunks");
  len = strlen(s);
  copy = (char *)sz_alloc(len + 1);
  memcpy(copy, s, len + 1);
  g_response[g_response_n].name = copy;
  g_response[g_response_n].trigger = (int64_t(*)(void))trigger_fn;
  g_response[g_response_n].response = (int64_t(*)(void))response_fn;
  g_response_n++;
}

void sz_verify_register(const char *name, SzVerdict *(*fn)(void *)) {
  const char *s = name ? name : "";
  size_t len;
  char *copy;
  if (!fn || !s[0])
    return;
  if (g_verify_n >= SZ_SESSION_MAX)
    sz_panic("sz_property session: too many verify predicates");
  len = strlen(s);
  copy = (char *)sz_alloc(len + 1);
  memcpy(copy, s, len + 1);
  g_verify[g_verify_n].name = copy;
  g_verify[g_verify_n].fn = fn;
  g_verify_n++;
}

void sz_verify_register_rel(const char *name, SzVerdict *(*fn)(void *, void *)) {
  const char *s = name ? name : "";
  size_t len;
  char *copy;
  if (!fn || !s[0])
    return;
  if (g_verify_rel_n >= SZ_SESSION_MAX)
    sz_panic("sz_property session: too many verify relations");
  len = strlen(s);
  copy = (char *)sz_alloc(len + 1);
  memcpy(copy, s, len + 1);
  g_verify_rel[g_verify_rel_n].name = copy;
  g_verify_rel[g_verify_rel_n].fn = fn;
  g_verify_rel_n++;
}

int sz_property_session_armed(void) {
  return g_always_n > 0 || g_eventually_n > 0 || g_response_n > 0 ||
         g_verify_n > 0;
}

void sz_property_session_step(void) {
  const char *tr = getenv("SCUZZ_TESTRT");
  if (!tr || tr[0] != '1')
    return;
  tl_push();
}

static void claim_fail(const char *kind, const char *name, int idx) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s failed: %s at state %d", kind,
           name ? name : "?", idx);
  fprintf(stderr, "scuzz: %s\n", buf);
  sz_panic(buf);
}
static void verdict_msg(char *buf, size_t cap, const char *name,
                        const SzVerdict *v) {
  const char *nm = name ? name : "?";
  if (v->index >= 0) {
    if (v->why)
      snprintf(buf, cap, "verify failed: %s at state %lld: %s", nm,
               (long long)v->index, v->why);
    else
      snprintf(buf, cap, "verify failed: %s at state %lld", nm,
               (long long)v->index);
  } else {
    if (v->why)
      snprintf(buf, cap, "verify failed: %s: %s", nm, v->why);
    else
      snprintf(buf, cap, "verify failed: %s", nm);
  }
}

static void claim_fail_verdict(const char *name, const SzVerdict *v) {
  char buf[256];
  verdict_msg(buf, sizeof buf, name, v);
  fprintf(stderr, "scuzz: %s\n", buf);
  sz_panic(buf);
}

void sz_property_session_end(void) {
  const char *tr = getenv("SCUZZ_TESTRT");
  const char *compact;
  int i;
  int j;
  int last;
  SzTimeline frozen;
  if (!tr || tr[0] != '1')
    return;
  if (g_effects_len > 0)
    tl_push();
  compact = getenv("SCUZZ_TIMELINE_COMPACT");
  if (compact && compact[0] == '1')
    sz_timeline_compact();
  tl_dump_file();
  sz_timeline_varied_flush();
  last = g_tl_n > 0 ? g_tl_n - 1 : 0;
  for (i = 0; i < g_always_n; i++) {
    for (j = 0; j < g_tl_n; j++) {
      tl_restore(j);
      if (g_always[i].fn && g_always[i].fn())
        continue;
      claim_fail("always", g_always[i].name, j);
    }
  }
  for (i = 0; i < g_eventually_n; i++) {
    int held = 0;
    for (j = 0; j < g_tl_n; j++) {
      tl_restore(j);
      if (g_eventually[i].fn && g_eventually[i].fn()) {
        held = 1;
        break;
      }
    }
    if (!held)
      claim_fail("eventually", g_eventually[i].name, last);
  }
  for (i = 0; i < g_response_n; i++) {
    int trig = -1;
    int held = 0;
    for (j = 0; j < g_tl_n; j++) {
      tl_restore(j);
      if (g_response[i].trigger && g_response[i].trigger()) {
        trig = j;
        break;
      }
    }
    if (trig < 0)
      continue;
    sometimes_record(g_response[i].name ? g_response[i].name : "?");
    for (j = trig; j < g_tl_n; j++) {
      tl_restore(j);
      if (g_response[i].response && g_response[i].response()) {
        held = 1;
        break;
      }
    }
    if (!held)
      claim_fail("response", g_response[i].name, last);
  }
  frozen.n = g_tl_n;
  frozen.fail_index = -1;
  frozen.states = g_tl;
  for (i = 0; i < g_verify_n; i++) {
    SzVerdict *v;
    if (!g_verify[i].fn)
      continue;
    v = g_verify[i].fn(&frozen);
    if (!v || v->valid)
      continue;
    claim_fail_verdict(g_verify[i].name, v);
  }
  tl_restore_clear();
}

void sz_property_session_reset(void) {
  int i;
  for (i = 0; i < g_always_n; i++) {
    if (g_always[i].name)
      sz_free(g_always[i].name);
    g_always[i].name = NULL;
    g_always[i].fn = NULL;
    g_always[i].held = 0;
  }
  g_always_n = 0;
  for (i = 0; i < g_eventually_n; i++) {
    if (g_eventually[i].name)
      sz_free(g_eventually[i].name);
    g_eventually[i].name = NULL;
    g_eventually[i].fn = NULL;
    g_eventually[i].held = 0;
  }
  g_eventually_n = 0;
  for (i = 0; i < g_response_n; i++) {
    if (g_response[i].name)
      sz_free(g_response[i].name);
    g_response[i].name = NULL;
    g_response[i].trigger = NULL;
    g_response[i].response = NULL;
  }
  g_response_n = 0;
  for (i = 0; i < g_verify_n; i++) {
    if (g_verify[i].name)
      sz_free(g_verify[i].name);
    g_verify[i].name = NULL;
    g_verify[i].fn = NULL;
  }
  g_verify_n = 0;
  for (i = 0; i < g_verify_rel_n; i++) {
    if (g_verify_rel[i].name)
      sz_free(g_verify_rel[i].name);
    g_verify_rel[i].name = NULL;
    g_verify_rel[i].fn = NULL;
  }
  g_verify_rel_n = 0;
  tl_free_states();
  free(g_last_drive);
  g_last_drive = NULL;
  tl_restore_clear();
}

/* Matches DRIVE_NAMES_MAX in crates/compiler/src/overlay.rs. Overlay rejects a larger table. */
#define SZ_DRIVERS_MAX 32
typedef struct {
  char *name;
  int nargs;
  int kind; /* 0=Int, 1=String, 2=Bool (i64 0/1) */
  void *fn;
} SzDriver;

static SzDriver g_drivers[SZ_DRIVERS_MAX];
static int g_drivers_n;

void sz_driver_register(SzString *name, int64_t nargs, int64_t kind, void *fn) {
  const char *s = name ? sz_string_cstr(name) : "";
  size_t n;
  char *copy;
  if (!fn || !s[0])
    return;
  if (g_drivers_n >= SZ_DRIVERS_MAX)
    sz_panic("sz_driver_register: too many drivers");
  n = strlen(s);
  copy = (char *)sz_alloc(n + 1);
  memcpy(copy, s, n + 1);
  g_drivers[g_drivers_n].name = copy;
  g_drivers[g_drivers_n].nargs = (int)nargs;
  g_drivers[g_drivers_n].kind = (int)kind;
  g_drivers[g_drivers_n].fn = fn;
  g_drivers_n++;
}

static SzDriver *sz_driver_find(const char *name) {
  int i;
  for (i = 0; i < g_drivers_n; i++) {
    if (strcmp(g_drivers[i].name, name) == 0)
      return &g_drivers[i];
  }
  return NULL;
}

static int64_t sz_driver_kind_at(int64_t packed, int i) {
  int j;
  int64_t p = 1;
  for (j = 0; j < i; j++)
    p *= 4;
  return (packed / p) % 4;
}

#define SZ_DRIVE_INNER 192

static int sz_drive_copy_range(const char *src, size_t n, char *out, int cap) {
  if (!out || cap <= 0)
    return 0;
  if (n >= (size_t)cap)
    n = (size_t)cap - 1;
  memcpy(out, src, n);
  out[n] = 0;
  return 1;
}

int sz_drive_uncons(const char *tok, const char *name, char *inner, int cap) {
  size_t nlen;
  const char *p;
  int depth;
  if (!tok || !name)
    return 0;
  nlen = strlen(name);
  if (strncmp(tok, name, nlen) != 0)
    return 0;
  if (tok[nlen] == 0) {
    if (inner && cap > 0)
      inner[0] = 0;
    return 1;
  }
  if (tok[nlen] != '(')
    return 0;
  p = tok + nlen + 1;
  depth = 0;
  while (*p) {
    if (*p == '(' || *p == '[')
      depth++;
    else if (*p == ')' || *p == ']') {
      if (depth == 0) {
        if (*p != ')')
          return 0;
        if (p[1] != 0)
          return 0;
        return sz_drive_copy_range(tok + nlen + 1, (size_t)(p - (tok + nlen + 1)),
                                   inner, cap);
      }
      depth--;
    }
    p++;
  }
  return 0;
}

int sz_drive_uncons_list(const char *tok, char *inner, int cap) {
  const char *p;
  int depth;
  if (!tok || tok[0] != '[')
    return 0;
  p = tok + 1;
  depth = 0;
  while (*p) {
    if (*p == '(' || *p == '[')
      depth++;
    else if (*p == ')' || *p == ']') {
      if (depth == 0) {
        if (*p != ']')
          return 0;
        if (p[1] != 0)
          return 0;
        return sz_drive_copy_range(tok + 1, (size_t)(p - (tok + 1)), inner, cap);
      }
      depth--;
    }
    p++;
  }
  return 0;
}

int64_t sz_drive_nfields(const char *inner) {
  int64_t n;
  int depth;
  int i;
  if (!inner || !inner[0])
    return 0;
  n = 1;
  depth = 0;
  for (i = 0; inner[i]; i++) {
    if (inner[i] == '(' || inner[i] == '[')
      depth++;
    else if ((inner[i] == ')' || inner[i] == ']') && depth > 0)
      depth--;
    else if (inner[i] == ',' && depth == 0)
      n++;
  }
  return n;
}

int sz_drive_field(const char *inner, int64_t idx, char *out, int cap) {
  int64_t cur = 0;
  int depth = 0;
  int i = 0;
  int start = 0;
  if (!inner || !out || cap <= 0 || idx < 0)
    return 0;
  out[0] = 0;
  while (inner[i]) {
    if (inner[i] == '(' || inner[i] == '[')
      depth++;
    else if ((inner[i] == ')' || inner[i] == ']') && depth > 0)
      depth--;
    else if (inner[i] == ',' && depth == 0) {
      if (cur == idx)
        return sz_drive_copy_range(inner + start, (size_t)(i - start), out, cap);
      cur++;
      start = i + 1;
    }
    i++;
  }
  if (cur == idx)
    return sz_drive_copy_range(inner + start, (size_t)(i - start), out, cap);
  return 0;
}

int64_t sz_drive_parse_int(const char *tok) {
  if (!tok || !tok[0])
    return 0;
  return (int64_t)strtoll(tok, NULL, 10);
}

int64_t sz_drive_parse_bool(const char *tok) {
  if (!tok)
    return 0;
  if (strcmp(tok, "true") == 0 || strcmp(tok, "1") == 0)
    return 1;
  return 0;
}

static int sz_driver_split_rest(const char *rest, char tok[][128], int maxn) {
  int n = 0;
  int i = 0;
  while (rest[i] && n < maxn) {
    while (rest[i] == ' ' || rest[i] == '\t' || rest[i] == '\n')
      i++;
    if (!rest[i])
      break;
    int k = 0;
    while (rest[i] && rest[i] != ' ' && rest[i] != '\t' && rest[i] != '\n' &&
           k < 127) {
      tok[n][k++] = rest[i++];
    }
    tok[n][k] = 0;
    n++;
  }
  return n;
}

void sz_driver_run_line(const char *spec) {
  char name[64];
  const char *rest;
  SzDriver *d;
  SzIo *io;
  SzIoResult r;
  int i = 0;
  if (!spec)
    spec = "";
  while (spec[i] && spec[i] != ' ' && i < 63) {
    name[i] = spec[i];
    i++;
  }
  name[i] = 0;
  rest = spec[i] == ' ' ? spec + i + 1 : "";
  d = sz_driver_find(name);
  if (!d) {
    fprintf(stderr, "scuzz: unknown driver %s\n", name);
    sz_panic("Ui.run: unknown driver");
  }
  if (d->nargs <= 0)
    io = ((SzIo * (*)(void)) d->fn)();
  else if (d->nargs == 1) {
    if (d->kind == 1)
      io = ((SzIo * (*)(SzString *)) d->fn)(sz_string_from_cstr(rest));
    else if (d->kind == 2)
      io = ((SzIo * (*)(int64_t)) d->fn)(
          (rest[0] && (strcmp(rest, "true") == 0 || strcmp(rest, "1") == 0))
              ? 1
              : 0);
    else
      io = ((SzIo * (*)(int64_t)) d->fn)((int64_t)atoi(rest));
  } else {
    char tok[4][128];
    int ntok = sz_driver_split_rest(rest, tok, 4);
    SzList *args = sz_list_nil();
    int j;
    for (j = d->nargs - 1; j >= 0; j--) {
      const char *t = j < ntok ? tok[j] : "";
      int64_t k = sz_driver_kind_at(d->kind, j);
      void *head;
      if (k == 1)
        head = sz_string_from_cstr(t);
      else if (k == 2)
        head = sz_box_i64((t[0] && (strcmp(t, "true") == 0 || strcmp(t, "1") == 0))
                              ? 1
                              : 0);
      else
        head = sz_box_i64((int64_t)atoi(t));
      {
        SzList *old = args;
        args = sz_list_cons(head, old);
        sz_release(head);
        sz_release(old);
      }
    }
    io = ((SzIo * (*)(SzList *)) d->fn)(args);
  }
  r = sz_io_unsafe_run(io);
  if (!r.ok) {
    fprintf(stderr, "scuzz: driver %s failed: %s\n", name,
            r.error ? sz_string_cstr(r.error->message) : "unknown");
    sz_panic("Ui.run: driver failed");
  }
  /* Driver work runs between pumps. Baseline the next idle frame. */
  sz_timeline_set_drive(spec);
  sz_testrt_ui_idle_reset();
  sz_property_session_step();
}

void sz_driver_run_script(const char *path) {
  FILE *f;
  char line[512];
  if (!path || !path[0])
    return;
  f = fopen(path, "r");
  if (!f)
    return;
  while (fgets(line, (int)sizeof(line), f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[n - 1] = 0;
      n--;
    }
    if (line[0] == 0 || line[0] == '#')
      continue;
    if (strncmp(line, "drive ", 6) == 0)
      sz_driver_run_line(line + 6);
    else
      sz_driver_run_line(line);
  }
  fclose(f);
}
