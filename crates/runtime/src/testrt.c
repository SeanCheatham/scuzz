#include "scuzz_rt.h"
#include "scuzz_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TestRuntime: install / reset fake interpreters. */

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

static SzIo *fail_drop(SzError *err) {
  SzIo *io = sz_io_fail(err);
  sz_release(err);
  return io;
}

static void *rc_box_zero(size_t n) {
  void *p = sz_rc_alloc(n, SZ_RC_BOX);
  memset(p, 0, n);
  return p;
}

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
static int g_fault_count_fs;
static int g_fault_count_net;
static int g_fault_count_queue;
static const char *g_fault_fail_msg;
static int g_idle_have;
static size_t g_idle_bytes;
static size_t g_idle_count;

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
    return;
  }
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

int sz_testrt_fault_mode(void) { return g_fault_mode; }

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

static int fs_fault(BoxResult *r) {
  if (!sz_testrt_fault_tick(SZ_FAULT_FS))
    return 0;
  r->is_err = 1;
  r->as.err = sz_error_new(2, "Fs: injected fault");
  return 1;
}

static SzString *pack_path(void *env) {
  SzPair *pack = (SzPair *)env;
  return pack ? (SzString *)pack->left : NULL;
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
  const char *req;
  sz_testrt_net_reset_live();
  g_net_fake = 1;
  req = getenv("SCUZZ_TESTRT_NET_REQUEST");
  sz_testrt_net_inject_request(req && req[0] ? req : "/");
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

/* SCUZZ_TESTRT_ENV is comma-separated KEY=val pairs. */
static void env_parse_spec(const char *spec) {
  const char *p;
  if (!spec)
    return;
  p = spec;
  while (*p) {
    const char *comma = strchr(p, ',');
    const char *eq;
    size_t n = comma ? (size_t)(comma - p) : strlen(p);
    char buf[256];
    if (n >= sizeof buf)
      n = sizeof buf - 1;
    memcpy(buf, p, n);
    buf[n] = '\0';
    eq = strchr(buf, '=');
    if (eq && eq != buf) {
      char key[128];
      size_t kn = (size_t)(eq - buf);
      if (kn >= sizeof key)
        kn = sizeof key - 1;
      memcpy(key, buf, kn);
      key[kn] = '\0';
      sz_testrt_env_set(key, eq + 1);
    }
    if (!comma)
      break;
    p = comma + 1;
  }
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
  const char *feed;
  const char *spec;
  sz_testrt_sys_reset_live();
  g_sys_fake = 1;
  feed = getenv("SCUZZ_TESTRT_STDIN");
  if (feed && feed[0])
    sz_testrt_stdin_feed(feed);
  /* App fixtures the CLI sets. Do not copy PATH/HOME/SCUZZ_TESTRT. */
  env_copy_host("SCUZZ_TODO_PATH");
  env_copy_host("SCUZZ_SERVE");
  env_copy_host("SCUZZ_KIT");
  spec = getenv("SCUZZ_TESTRT_ENV");
  if (spec && spec[0])
    env_parse_spec(spec);
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
  g_fault_count_fs = 0;
  g_fault_count_net = 0;
  g_fault_count_queue = 0;
  g_fault_fail_msg = NULL;
  g_idle_have = 0;
  g_idle_bytes = 0;
  g_idle_count = 0;
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
  if (g_property_a11y) {
    sz_free(g_property_a11y);
    g_property_a11y = NULL;
  }
  if (!dump)
    dump = "";
  n = strlen(dump);
  g_property_a11y = (char *)sz_alloc(n + 1);
  memcpy(g_property_a11y, dump, n + 1);
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
  if (g_property_last_hit) {
    sz_free(g_property_last_hit);
    g_property_last_hit = NULL;
  }
  if (!desc)
    desc = "";
  n = strlen(desc);
  g_property_last_hit = (char *)sz_alloc(n + 1);
  memcpy(g_property_last_hit, desc, n + 1);
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
  copy = (char *)sz_alloc(n + 1);
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
  copy = (char *)sz_alloc(n + 1);
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
  int64_t (*fn)(void *);
} SzVerifyProp;

static SzVerifyProp g_verify[SZ_SESSION_MAX];
static int g_verify_n;

typedef struct {
  char *signals;
  char *a11y;
  char *last_hit;
  char *drive;
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

static void tl_free_states(void) {
  int i;
  for (i = 0; i < g_tl_n; i++) {
    free(g_tl[i].signals);
    free(g_tl[i].a11y);
    free(g_tl[i].last_hit);
    free(g_tl[i].drive);
  }
  free(g_tl);
  g_tl = NULL;
  g_tl_n = 0;
  g_tl_cap = 0;
  g_tl_warned = 0;
}

void sz_timeline_set_drive(const char *line) {
  free(g_last_drive);
  g_last_drive = tl_copy(line ? line : "");
}

static void tl_push(void) {
  SzString *dump;
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
  g_tl[g_tl_n].signals = tl_copy(dump ? sz_string_cstr(dump) : "");
  if (dump)
    sz_string_free(dump);
  g_tl[g_tl_n].a11y = tl_copy(g_property_a11y);
  g_tl[g_tl_n].last_hit = tl_copy(g_property_last_hit);
  g_tl[g_tl_n].drive = tl_copy(g_last_drive);
  g_tl_n++;
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

static void tl_dump_file(void) {
  const char *path = getenv("SCUZZ_TIMELINE_DUMP");
  FILE *f;
  int i;
  if (!path || !path[0])
    return;
  f = fopen(path, "w");
  if (!f)
    return;
  fprintf(f, "# timeline n=%d\n", g_tl_n);
  for (i = 0; i < g_tl_n; i++) {
    fprintf(f, "--- %d\n", i);
    fprintf(f, "last_hit:\n%s\n", g_tl[i].last_hit ? g_tl[i].last_hit : "");
    fprintf(f, "drive:\n%s\n", g_tl[i].drive ? g_tl[i].drive : "");
    fprintf(f, "signals:\n%s", g_tl[i].signals ? g_tl[i].signals : "");
    if (g_tl[i].signals && g_tl[i].signals[0] &&
        g_tl[i].signals[strlen(g_tl[i].signals) - 1] != '\n')
      fputc('\n', f);
    fprintf(f, "a11y:\n%s", g_tl[i].a11y ? g_tl[i].a11y : "");
    if (g_tl[i].a11y && g_tl[i].a11y[0] &&
        g_tl[i].a11y[strlen(g_tl[i].a11y) - 1] != '\n')
      fputc('\n', f);
  }
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

void sz_verify_register(SzString *name, void *fn) {
  const char *s = name ? sz_string_cstr(name) : "";
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
  g_verify[g_verify_n].fn = (int64_t(*)(void *))fn;
  g_verify_n++;
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

void sz_property_session_end(void) {
  const char *tr = getenv("SCUZZ_TESTRT");
  char buf[256];
  int i;
  int j;
  int last;
  SzTimeline frozen;
  if (!tr || tr[0] != '1')
    return;
  tl_dump_file();
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
    frozen.fail_index = -1;
    if (g_verify[i].fn && g_verify[i].fn(&frozen))
      continue;
    if (frozen.fail_index >= 0)
      claim_fail("verify", g_verify[i].name, frozen.fail_index);
    snprintf(buf, sizeof buf, "verify failed: %s",
             g_verify[i].name ? g_verify[i].name : "?");
    fprintf(stderr, "scuzz: %s\n", buf);
    sz_panic(buf);
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
