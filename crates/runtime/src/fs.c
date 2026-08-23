#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Blessed filesystem IO — live interpreter or TestRuntime mem FS.
 * Fake vs live is chosen when the IO runs (after sz_testrt_install in
 * runtime_main), not when the graph is built. */

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

typedef struct {
  int is_err;
  union {
    SzError *err;
    void *ok;
  } as;
} FsResult;

static SzIo *unwrap_fs(void *value, void *env) {
  (void)env;
  FsResult *r = (FsResult *)value;
  if (!r)
    return sz_io_fail_cstr("Fs: null result");
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

static void *fs_dispatch(void *env);

static SzString *pack_path(void *env) {
  SzPair *pack = (SzPair *)env;
  return pack ? (SzString *)pack->left : NULL;
}

static SzIo *fs_bind(SzString *path, SzCont after) {
  SzPair *pack;
  if (!path)
    sz_panic("Fs: null path");
  pack = sz_pair_new(path, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(fs_dispatch, NULL), after, pack);
    sz_release(pack);
    return io;
  }
}

static void *fs_read_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  FILE *f = fopen(p, "rb");
  struct stat st;
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.read: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
    fclose(f);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: not a regular file");
    goto done;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: seek end failed");
    goto done;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: ftell failed");
    goto done;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: seek set failed");
    goto done;
  }
  char *buf = (char *)sz_alloc((size_t)sz + 1);
  size_t n = fread(buf, 1, (size_t)sz, f);
  if (ferror(f)) {
    fclose(f);
    sz_free(buf);
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.read: read failed");
    goto done;
  }
  fclose(f);
  buf[n] = '\0';
  SzString *s = sz_string_from_bytes(buf, n);
  sz_free(buf);
  r->is_err = 0;
  r->as.ok = s;
done:
  return r;
}

static void *fs_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_fs_is_fake() ? 1 : 0);
}

static SzIo *fs_after_read(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_read(pack_path(pack));
  return fm_drop(sz_io_delay(fs_read_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_read(SzString *path) { return fs_bind(path, fs_after_read); }

static void *fs_write_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = (SzString *)pack->left;
  SzString *contents = (SzString *)pack->right;
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  FILE *f = fopen(p, "wb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.write: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  if (contents && contents->len) {
    if (fwrite(contents->data, 1, contents->len, f) != contents->len) {
      fclose(f);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.write: short write");
      goto done;
    }
  }
  fclose(f);
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

static SzIo *fs_after_write(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_write((SzString *)pack->left, (SzString *)pack->right);
  return fm_drop(sz_io_delay(fs_write_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_write(SzString *path, SzString *contents) {
  SzString *body;
  SzPair *pack;
  if (!path)
    sz_panic("sz_fs_write(null path)");
  body = contents ? contents : sz_string_from_cstr("");
  pack = sz_pair_new(path, body);
  if (!contents)
    sz_release(body);
  {
    SzIo *io = fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_write, pack);
    sz_release(pack);
    return io;
  }
}

static SzList *cons_fs_entry(SzList *acc, const char *name, int is_dir) {
  SzString *s = sz_string_from_cstr(name);
  void *flag = sz_box_i64(is_dir ? 1 : 0);
  SzPair *ent = sz_pair_new(s, flag);
  SzList *old = acc;
  SzList *out = sz_list_cons(ent, old);
  sz_release(s);
  sz_release(flag);
  sz_release(ent);
  sz_release(old);
  return out;
}

static int fs_name_is_dir(const char *dir, const char *name) {
  char full[2048];
  struct stat st;
  size_t n = strlen(dir);
  if (n == 1 && dir[0] == '/')
    snprintf(full, sizeof full, "/%s", name);
  else if (n > 0 && dir[n - 1] == '/')
    snprintf(full, sizeof full, "%s%s", dir, name);
  else if (n == 0 || (n == 1 && dir[0] == '.'))
    snprintf(full, sizeof full, "%s", name);
  else
    snprintf(full, sizeof full, "%s/%s", dir, name);
  if (stat(full, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

static void *fs_list_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  DIR *d = opendir(p);
  if (!d) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.list: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  SzList *acc = sz_list_nil();
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    acc = cons_fs_entry(acc, ent->d_name, fs_name_is_dir(p, ent->d_name));
  }
  closedir(d);
  r->is_err = 0;
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    r->as.ok = rev;
  }
done:
  return r;
}

static SzIo *fs_after_list(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_list(pack_path(pack));
  return fm_drop(sz_io_delay(fs_list_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_list(SzString *path) { return fs_bind(path, fs_after_list); }

/* Make `path` a directory. An existing directory is ok. A file is an error. */
static int fs_mkdir_one(const char *path, FsResult *r) {
  struct stat st;
  if (mkdir(path, 0755) == 0)
    return 1;
  if (errno != EEXIST) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.mkdirs: %s: %s", path, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    return 0;
  }
  if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: exists and is not a directory");
    return 0;
  }
  return 1;
}

static void *fs_mkdirs_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  char tmp[1024];
  size_t len = strlen(p);
  if (len >= sizeof(tmp)) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.mkdirs: path too long");
    goto done;
  }
  memcpy(tmp, p, len + 1);
  for (char *q = tmp + 1; *q; q++) {
    if (*q == '/') {
      *q = '\0';
      if (!fs_mkdir_one(tmp, r))
        goto done;
      *q = '/';
    }
  }
  if (!fs_mkdir_one(tmp, r))
    goto done;
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

static SzIo *fs_after_mkdirs(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_mkdirs(pack_path(pack));
  return fm_drop(sz_io_delay(fs_mkdirs_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_mkdirs(SzString *path) { return fs_bind(path, fs_after_mkdirs); }

static void *fs_canonicalize_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  char *resolved = realpath(p, NULL);
  if (!resolved) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.canonicalize: %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(resolved);
  free(resolved);
done:
  return r;
}

static SzIo *fs_after_canonicalize(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_canonicalize(pack_path(pack));
  return fm_drop(sz_io_delay(fs_canonicalize_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_canonicalize(SzString *path) {
  return fs_bind(path, fs_after_canonicalize);
}

static void *fs_exists_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  struct stat st;
  r->is_err = 0;
  r->as.ok = sz_box_i64(stat(p, &st) == 0 ? 1 : 0);
  return r;
}

static SzIo *fs_after_exists(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_exists(pack_path(pack));
  return fm_drop(sz_io_delay(fs_exists_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_exists(SzString *path) { return fs_bind(path, fs_after_exists); }

static SzString *fs_copy_cstr(const char *s) {
  return sz_string_from_cstr(s ? s : "");
}

SzString *sz_fs_join(SzString *a, SzString *b) {
  const char *left;
  const char *right;
  size_t ln, rn;
  char *out;
  SzString *s;
  if (!a || !b)
    sz_panic("Fs.join(null)");
  left = sz_string_cstr(a);
  right = sz_string_cstr(b);
  if (!right[0])
    return fs_copy_cstr(left);
  if (right[0] == '/')
    return fs_copy_cstr(right);
  if (!left[0] || strcmp(left, ".") == 0)
    return fs_copy_cstr(right);
  ln = strlen(left);
  rn = strlen(right);
  if (left[ln - 1] == '/') {
    out = (char *)sz_alloc(ln + rn + 1);
    memcpy(out, left, ln);
    memcpy(out + ln, right, rn + 1);
  } else {
    out = (char *)sz_alloc(ln + 1 + rn + 1);
    memcpy(out, left, ln);
    out[ln] = '/';
    memcpy(out + ln + 1, right, rn + 1);
  }
  s = sz_string_from_cstr(out);
  sz_free(out);
  return s;
}

static void fs_strip_trailing_slash(char *s) {
  size_t n;
  if (!s)
    return;
  n = strlen(s);
  while (n > 1 && s[n - 1] == '/') {
    s[n - 1] = '\0';
    n--;
  }
}

SzString *sz_fs_dirname(SzString *path) {
  const char *p;
  char *buf;
  char *slash;
  SzString *s;
  if (!path)
    sz_panic("Fs.dirname(null)");
  p = sz_string_cstr(path);
  if (!p[0])
    return fs_copy_cstr(".");
  buf = (char *)sz_alloc(strlen(p) + 1);
  memcpy(buf, p, strlen(p) + 1);
  fs_strip_trailing_slash(buf);
  slash = strrchr(buf, '/');
  if (!slash) {
    sz_free(buf);
    return fs_copy_cstr(".");
  }
  if (slash == buf) {
    slash[1] = '\0';
    s = sz_string_from_cstr(buf);
    sz_free(buf);
    return s;
  }
  *slash = '\0';
  s = sz_string_from_cstr(buf);
  sz_free(buf);
  return s;
}

SzString *sz_fs_basename(SzString *path) {
  const char *p;
  char *buf;
  char *slash;
  SzString *s;
  if (!path)
    sz_panic("Fs.basename(null)");
  p = sz_string_cstr(path);
  if (!p[0])
    return fs_copy_cstr("");
  buf = (char *)sz_alloc(strlen(p) + 1);
  memcpy(buf, p, strlen(p) + 1);
  fs_strip_trailing_slash(buf);
  if (buf[0] == '/' && buf[1] == '\0') {
    s = sz_string_from_cstr("/");
    sz_free(buf);
    return s;
  }
  slash = strrchr(buf, '/');
  s = sz_string_from_cstr(slash ? slash + 1 : buf);
  sz_free(buf);
  return s;
}
