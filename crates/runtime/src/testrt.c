#include "scalui_rt.h"

#include <stdio.h>
#include <string.h>

/* TestRuntime: install / reset fake interpreters. */

void su_testrt_clock_reset_live(void);
void su_testrt_random_reset_live(void);
void su_testrt_fs_reset_live(void);
void su_testrt_net_reset_live(void);

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

void su_testrt_fs_reset_live(void) {
  MemNode *n = g_fs;
  while (n) {
    MemNode *next = n->next;
    su_free(n->path);
    su_free(n->data);
    su_free(n);
    n = next;
  }
  g_fs = NULL;
  g_fs_fake = 0;
}

void su_testrt_fs_install(void) {
  su_testrt_fs_reset_live();
  g_fs_fake = 1;
  /* Root directory exists. */
  {
    MemNode *root = (MemNode *)su_alloc(sizeof(MemNode));
    root->path = (char *)su_alloc(1);
    root->path[0] = '\0';
    root->is_dir = 1;
    root->data = NULL;
    root->len = 0;
    root->next = NULL;
    g_fs = root;
  }
}

int su_testrt_fs_is_fake(void) { return g_fs_fake; }

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
  out = (char *)su_alloc(n + 1);
  j = 0;
  for (i = 0; i < n; i++) {
    if (p[i] == '/' && j > 0 && out[j - 1] == '/')
      continue;
    out[j++] = p[i];
  }
  while (j > 0 && out[j - 1] == '/')
    j--;
  out[j] = '\0';
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
  n = (MemNode *)su_alloc(sizeof(MemNode));
  n->path = (char *)su_alloc(strlen(path) + 1);
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
    SuError *err;
    void *ok;
  } as;
} BoxResult;

static SuIo *unwrap_box(void *value, void *env) {
  (void)env;
  BoxResult *r = (BoxResult *)value;
  if (!r)
    return su_io_fail_cstr("TestRuntime: null result");
  if (r->is_err)
    return su_io_fail(r->as.err);
  return su_io_pure(r->as.ok);
}

static void *mem_read(void *env) {
  SuString *path_s = (SuString *)env;
  BoxResult *r = (BoxResult *)su_alloc(sizeof(BoxResult));
  char *path = norm_path(su_string_cstr(path_s));
  MemNode *n = fs_find(path);
  su_free(path);
  if (!n || n->is_dir) {
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.read: not found (mem)");
    return r;
  }
  r->is_err = 0;
  r->as.ok = su_string_from_bytes(n->data ? n->data : "", n->len);
  return r;
}

SuIo *su_testrt_fs_read(SuString *path) {
  return su_io_flatmap(su_io_delay(mem_read, path), unwrap_box, NULL);
}

typedef struct {
  SuString *path;
  SuString *contents;
} WriteEnv;

static void *mem_write(void *env) {
  WriteEnv *e = (WriteEnv *)env;
  BoxResult *r = (BoxResult *)su_alloc(sizeof(BoxResult));
  char *path = norm_path(su_string_cstr(e->path));
  char parent[1024];
  MemNode *n;
  SuString *c = e->contents ? e->contents : su_string_from_cstr("");

  if (!parent_path(path, parent, sizeof parent)) {
    su_free(path);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.write: path too long (mem)");
    return r;
  }
  if (parent[0] != '\0' && !fs_find(parent)) {
    /* Ensure parent dirs exist (mkdir -p style for write). */
    char tmp[1024];
    size_t len = strlen(parent);
    size_t i;
    if (len >= sizeof tmp) {
      su_free(path);
      r->is_err = 1;
      r->as.err = su_error_new(2, "Fs.write: parent too long (mem)");
      return r;
    }
    memcpy(tmp, parent, len + 1);
    for (i = 1; i < len; i++) {
      if (tmp[i] == '/') {
        tmp[i] = '\0';
        if (!fs_ensure_dir(tmp)) {
          su_free(path);
          r->is_err = 1;
          r->as.err = su_error_new(2, "Fs.write: parent is file (mem)");
          return r;
        }
        tmp[i] = '/';
      }
    }
    if (!fs_ensure_dir(parent)) {
      su_free(path);
      r->is_err = 1;
      r->as.err = su_error_new(2, "Fs.write: parent is file (mem)");
      return r;
    }
  }

  n = fs_find(path);
  if (n && n->is_dir) {
    su_free(path);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.write: is a directory (mem)");
    return r;
  }
  if (!n) {
    n = (MemNode *)su_alloc(sizeof(MemNode));
    n->path = path;
    n->is_dir = 0;
    n->data = NULL;
    n->len = 0;
    n->next = g_fs;
    g_fs = n;
  } else {
    su_free(path);
    su_free(n->data);
    n->data = NULL;
    n->len = 0;
  }
  n->len = c->len;
  n->data = (char *)su_alloc(c->len + 1);
  if (c->len)
    memcpy(n->data, c->data, c->len);
  n->data[c->len] = '\0';
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

SuIo *su_testrt_fs_write(SuString *path, SuString *contents) {
  WriteEnv *e = (WriteEnv *)su_alloc(sizeof(WriteEnv));
  e->path = path;
  e->contents = contents;
  return su_io_flatmap(su_io_delay(mem_write, e), unwrap_box, NULL);
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
  SuString *path_s = (SuString *)env;
  BoxResult *r = (BoxResult *)su_alloc(sizeof(BoxResult));
  char *path = norm_path(su_string_cstr(path_s));
  MemNode *dir = fs_find(path);
  MemNode *n;
  SuList *acc;

  if (!dir || !dir->is_dir) {
    su_free(path);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.list: not a directory (mem)");
    return r;
  }
  acc = su_list_nil();
  for (n = g_fs; n; n = n->next) {
    if (n == dir)
      continue;
    if (is_direct_child(path, n->path)) {
      const char *base = strrchr(n->path, '/');
      base = base ? base + 1 : n->path;
      acc = su_list_cons(su_string_from_cstr(base), acc);
    }
  }
  su_free(path);
  r->is_err = 0;
  r->as.ok = su_list_reverse(acc);
  return r;
}

SuIo *su_testrt_fs_list(SuString *path) {
  return su_io_flatmap(su_io_delay(mem_list, path), unwrap_box, NULL);
}

static void *mem_mkdirs(void *env) {
  SuString *path_s = (SuString *)env;
  BoxResult *r = (BoxResult *)su_alloc(sizeof(BoxResult));
  char *path = norm_path(su_string_cstr(path_s));
  char tmp[1024];
  size_t len = strlen(path);
  size_t i;

  if (len == 0) {
    su_free(path);
    r->is_err = 0;
    r->as.ok = NULL;
    return r;
  }
  if (len >= sizeof tmp) {
    su_free(path);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.mkdirs: path too long (mem)");
    return r;
  }
  memcpy(tmp, path, len + 1);
  for (i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      if (!fs_ensure_dir(tmp)) {
        su_free(path);
        r->is_err = 1;
        r->as.err = su_error_new(2, "Fs.mkdirs: path component is file (mem)");
        return r;
      }
      tmp[i] = '/';
    }
  }
  if (!fs_ensure_dir(path)) {
    su_free(path);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.mkdirs: path is file (mem)");
    return r;
  }
  su_free(path);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

SuIo *su_testrt_fs_mkdirs(SuString *path) {
  return su_io_flatmap(su_io_delay(mem_mkdirs, path), unwrap_box, NULL);
}

/* --- stub network -------------------------------------------------------- */

typedef struct NetStub {
  char *url;
  char *body;
  struct NetStub *next;
} NetStub;

static int g_net_fake = 0;
static NetStub *g_stubs = NULL;

void su_testrt_net_reset_live(void) {
  NetStub *s = g_stubs;
  while (s) {
    NetStub *n = s->next;
    su_free(s->url);
    su_free(s->body);
    su_free(s);
    s = n;
  }
  g_stubs = NULL;
  g_net_fake = 0;
}

void su_testrt_net_install(void) {
  su_testrt_net_reset_live();
  g_net_fake = 1;
}

int su_testrt_net_is_fake(void) { return g_net_fake; }

void su_testrt_net_stub(const char *url, const char *body) {
  NetStub *s;
  if (!g_net_fake)
    su_testrt_net_install();
  s = (NetStub *)su_alloc(sizeof(NetStub));
  s->url = (char *)su_alloc(strlen(url ? url : "") + 1);
  memcpy(s->url, url ? url : "", strlen(url ? url : "") + 1);
  s->body = (char *)su_alloc(strlen(body ? body : "") + 1);
  memcpy(s->body, body ? body : "", strlen(body ? body : "") + 1);
  s->next = g_stubs;
  g_stubs = s;
}

static void *stub_http_get(void *env) {
  SuString *url_s = (SuString *)env;
  BoxResult *r = (BoxResult *)su_alloc(sizeof(BoxResult));
  const char *url = su_string_cstr(url_s);
  NetStub *s;
  for (s = g_stubs; s; s = s->next) {
    if (strcmp(s->url, url) == 0) {
      r->is_err = 0;
      r->as.ok = su_string_from_cstr(s->body);
      return r;
    }
  }
  r->is_err = 1;
  r->as.err = su_error_new(6, "Net.httpGet: no stub for URL");
  return r;
}

SuIo *su_testrt_net_http_get(SuString *url) {
  return su_io_flatmap(su_io_delay(stub_http_get, url), unwrap_box, NULL);
}

/* --- install / reset ----------------------------------------------------- */

void su_testrt_install(void) {
  su_testrt_clock_install(1);
  su_testrt_random_install(42);
  su_testrt_fs_install();
  su_testrt_net_install();
}

void su_testrt_reset(void) {
  su_testrt_clock_reset_live();
  su_testrt_random_reset_live();
  su_testrt_fs_reset_live();
  su_testrt_net_reset_live();
}
