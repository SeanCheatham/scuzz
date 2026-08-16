#include "scuzz_rt.h"

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

static SzIo *unwrap_box(void *value, void *env) {
  (void)env;
  BoxResult *r = (BoxResult *)value;
  if (!r)
    return sz_io_fail_cstr("TestRuntime: null result");
  if (r->is_err)
    return sz_io_fail(r->as.err);
  return sz_io_pure(r->as.ok);
}

static void *mem_read(void *env) {
  SzString *path_s = (SzString *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  char *path = norm_path(sz_string_cstr(path_s));
  MemNode *n = fs_find(path);
  sz_free(path);
  if (!n || n->is_dir) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: not found (mem)");
    return r;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_bytes(n->data ? n->data : "", n->len);
  return r;
}

SzIo *sz_testrt_fs_read(SzString *path) {
  return sz_io_flatmap(sz_io_delay(mem_read, path), unwrap_box, NULL);
}

typedef struct {
  SzString *path;
  SzString *contents;
} WriteEnv;

static void *mem_write(void *env) {
  WriteEnv *e = (WriteEnv *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  char *path = norm_path(sz_string_cstr(e->path));
  char parent[1024];
  MemNode *n;
  SzString *c = e->contents ? e->contents : sz_string_from_cstr("");

  if (!parent_path(path, parent, sizeof parent)) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.write: path too long (mem)");
    return r;
  }
  if (parent[0] != '\0' && !fs_find(parent)) {
    /* Create parent dirs (mkdir -p style for write). */
    char tmp[1024];
    size_t len = strlen(parent);
    size_t i;
    if (len >= sizeof tmp) {
      sz_free(path);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.write: parent too long (mem)");
      return r;
    }
    memcpy(tmp, parent, len + 1);
    for (i = 1; i < len; i++) {
      if (tmp[i] == '/') {
        tmp[i] = '\0';
        if (!fs_ensure_dir(tmp)) {
          sz_free(path);
          r->is_err = 1;
          r->as.err = sz_error_new(2, "Fs.write: parent is file (mem)");
          return r;
        }
        tmp[i] = '/';
      }
    }
    if (!fs_ensure_dir(parent)) {
      sz_free(path);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.write: parent is file (mem)");
      return r;
    }
  }

  n = fs_find(path);
  if (n && n->is_dir) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.write: is a directory (mem)");
    return r;
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
  return r;
}

SzIo *sz_testrt_fs_write(SzString *path, SzString *contents) {
  WriteEnv *e = (WriteEnv *)sz_alloc(sizeof(WriteEnv));
  e->path = path;
  e->contents = contents;
  return sz_io_flatmap(sz_io_delay(mem_write, e), unwrap_box, NULL);
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
  SzString *path_s = (SzString *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  char *path = norm_path(sz_string_cstr(path_s));
  MemNode *dir = fs_find(path);
  MemNode *n;
  SzList *acc;

  if (!dir || !dir->is_dir) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.list: not a directory (mem)");
    return r;
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
        SzList *old = acc;
        acc = sz_list_cons(s, old);
        sz_release(s);
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
  return r;
}

SzIo *sz_testrt_fs_list(SzString *path) {
  return sz_io_flatmap(sz_io_delay(mem_list, path), unwrap_box, NULL);
}

static void *mem_mkdirs(void *env) {
  SzString *path_s = (SzString *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  char *path = norm_path(sz_string_cstr(path_s));
  char tmp[1024];
  size_t len = strlen(path);
  size_t i;

  if (len == 0) {
    sz_free(path);
    r->is_err = 0;
    r->as.ok = NULL;
    return r;
  }
  if (len >= sizeof tmp) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: path too long (mem)");
    return r;
  }
  memcpy(tmp, path, len + 1);
  for (i = 1; i < len; i++) {
    if (tmp[i] == '/') {
      tmp[i] = '\0';
      if (!fs_ensure_dir(tmp)) {
        sz_free(path);
        r->is_err = 1;
        r->as.err = sz_error_new(2, "Fs.mkdirs: path component is file (mem)");
        return r;
      }
      tmp[i] = '/';
    }
  }
  if (!fs_ensure_dir(path)) {
    sz_free(path);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: path is file (mem)");
    return r;
  }
  sz_free(path);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

SzIo *sz_testrt_fs_mkdirs(SzString *path) {
  return sz_io_flatmap(sz_io_delay(mem_mkdirs, path), unwrap_box, NULL);
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
        sz_free(seg);
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
  SzString *path_s = (SzString *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  char *path = canon_path(sz_string_cstr(path_s));
  MemNode *n = fs_find(path);
  if (!n) {
    /* Allow canonicalize of a missing leaf if the parent exists as a dir.
       Match common realpath failure modes loosely: need an exact mem node. */
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.canonicalize: not found (mem)");
    sz_free(path);
    return r;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(path);
  sz_free(path);
  return r;
}

SzIo *sz_testrt_fs_canonicalize(SzString *path) {
  return sz_io_flatmap(sz_io_delay(mem_canonicalize, path), unwrap_box, NULL);
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
  if (!g_net_fake)
    sz_testrt_net_install();
  s = (NetStub *)sz_alloc(sizeof(NetStub));
  s->url = (char *)sz_alloc(strlen(url ? url : "") + 1);
  memcpy(s->url, url ? url : "", strlen(url ? url : "") + 1);
  s->body = (char *)sz_alloc(strlen(body ? body : "") + 1);
  memcpy(s->body, body ? body : "", strlen(body ? body : "") + 1);
  s->next = g_stubs;
  g_stubs = s;
}

static void *stub_http_get(void *env) {
  SzString *url_s = (SzString *)env;
  BoxResult *r = (BoxResult *)sz_alloc(sizeof(BoxResult));
  const char *url = sz_string_cstr(url_s);
  NetStub *s;
  for (s = g_stubs; s; s = s->next) {
    if (strcmp(s->url, url) == 0) {
      r->is_err = 0;
      r->as.ok = sz_string_from_cstr(s->body);
      return r;
    }
  }
  r->is_err = 1;
  r->as.err = sz_error_new(6, "Net.httpGet: no stub for URL");
  return r;
}

SzIo *sz_testrt_net_http_get(SzString *url) {
  return sz_io_flatmap(sz_io_delay(stub_http_get, url), unwrap_box, NULL);
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
  int64_t n = *(int64_t *)env;
  size_t want;
  size_t avail;
  size_t take;
  void *s;
  sz_free(env);
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
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = n;
  return sz_io_delay(testrt_read_n_thunk, p);
}

static void sz_testrt_sys_install(void) {
  const char *feed;
  sz_testrt_sys_reset_live();
  g_sys_fake = 1;
  feed = getenv("SCUZZ_TESTRT_STDIN");
  if (feed && feed[0])
    sz_testrt_stdin_feed(feed);
}

int sz_testrt_sys_is_fake(void) { return g_sys_fake; }

/* --- install / reset ----------------------------------------------------- */

void sz_testrt_install(void) {
  sz_testrt_clock_install(1);
  sz_testrt_random_install(42);
  sz_testrt_fs_install();
  sz_testrt_net_install();
  sz_testrt_sys_install();
}

void sz_testrt_reset(void) {
  sz_testrt_clock_reset_live();
  sz_testrt_random_reset_live();
  sz_testrt_fs_reset_live();
  sz_testrt_net_reset_live();
  sz_testrt_sys_reset_live();
}

/* --- residual laws (armed under SCUZZ_TESTRT=1) -------------------------- */

static char *g_law_a11y = NULL;

void sz_law_stash_a11y(const char *dump) {
  size_t n;
  if (g_law_a11y) {
    sz_free(g_law_a11y);
    g_law_a11y = NULL;
  }
  if (!dump)
    dump = "";
  n = strlen(dump);
  g_law_a11y = (char *)sz_alloc(n + 1);
  memcpy(g_law_a11y, dump, n + 1);
}

int64_t sz_law_a11y_has(SzString *needle) {
  const char *n = needle ? sz_string_cstr(needle) : "";
  if (!g_law_a11y || !n[0])
    return 0;
  return strstr(g_law_a11y, n) != NULL ? 1 : 0;
}

SzIo *sz_law_assert(SzString *name, int64_t ok) {
  const char *tr = getenv("SCUZZ_TESTRT");
  char buf[256];
  if (!tr || tr[0] != '1')
    return sz_io_pure(NULL);
  if (ok)
    return sz_io_pure(NULL);
  snprintf(buf, sizeof buf, "law failed: %s", name ? sz_string_cstr(name) : "?");
  fprintf(stderr, "scuzz: %s\n", buf);
  return sz_io_fail_cstr(buf);
}

void sz_law_check(SzString *name, int64_t ok) {
  const char *tr = getenv("SCUZZ_TESTRT");
  char buf[256];
  if (!tr || tr[0] != '1')
    return;
  if (ok)
    return;
  snprintf(buf, sizeof buf, "law check failed: %s",
           name ? sz_string_cstr(name) : "?");
  fprintf(stderr, "scuzz: %s\n", buf);
  sz_panic(buf);
}

#define SZ_SOMETIMES_MAX 64
static char *g_sometimes[SZ_SOMETIMES_MAX];
static int g_sometimes_n;

void sz_law_sometimes(SzString *name) {
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
}

void sz_law_sometimes_flush(void) {
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
  if (!fn || !s[0] || g_drivers_n >= SZ_DRIVERS_MAX)
    return;
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

static int sz_driver_split_rest(const char *rest, char tok[][64], int maxn) {
  int n = 0;
  int i = 0;
  while (rest[i] && n < maxn) {
    while (rest[i] == ' ' || rest[i] == '\t' || rest[i] == '\n')
      i++;
    if (!rest[i])
      break;
    int k = 0;
    while (rest[i] && rest[i] != ' ' && rest[i] != '\t' && rest[i] != '\n' &&
           k < 63) {
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
    char tok[4][64];
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
}

void sz_driver_run_script(const char *path) {
  FILE *f;
  char line[256];
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
