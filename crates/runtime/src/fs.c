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
  if (r->is_err)
    return fail_drop(r->as.err);
  return pure_drop(r->as.ok);
}

static void *fs_read_result(void *env) {
  SzString *path = (SzString *)env;
  FsResult *r = (FsResult *)sz_alloc(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  FILE *f = fopen(p, "rb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.read: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
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
  fclose(f);
  buf[n] = '\0';
  SzString *s = sz_string_from_bytes(buf, n);
  sz_free(buf);
  r->is_err = 0;
  r->as.ok = s;
done:
  sz_release(path);
  return r;
}

static void *fs_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_fs_is_fake() ? 1 : 0);
}

static SzIo *fs_after_read(void *value, void *env) {
  if ((intptr_t)value)
    return sz_testrt_fs_read((SzString *)env);
  return fm_drop(sz_io_delay(fs_read_result, env), unwrap_fs, NULL);
}

SzIo *sz_fs_read(SzString *path) {
  if (!path)
    sz_panic("sz_fs_read(null)");
  return fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_read, path);
}

static void *fs_write_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = (SzString *)pack->left;
  SzString *contents = (SzString *)pack->right;
  FsResult *r = (FsResult *)sz_alloc(sizeof(FsResult));
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
  sz_release(pack);
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

static void *fs_list_result(void *env) {
  SzString *path = (SzString *)env;
  FsResult *r = (FsResult *)sz_alloc(sizeof(FsResult));
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
    {
      SzString *s = sz_string_from_cstr(ent->d_name);
      SzList *old = acc;
      acc = sz_list_cons(s, old);
      sz_release(s);
      sz_release(old);
    }
  }
  closedir(d);
  r->is_err = 0;
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    r->as.ok = rev;
  }
done:
  sz_release(path);
  return r;
}

static SzIo *fs_after_list(void *value, void *env) {
  if ((intptr_t)value)
    return sz_testrt_fs_list((SzString *)env);
  return fm_drop(sz_io_delay(fs_list_result, env), unwrap_fs, NULL);
}

SzIo *sz_fs_list(SzString *path) {
  if (!path)
    sz_panic("sz_fs_list(null)");
  return fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_list, path);
}

static void *fs_mkdirs_result(void *env) {
  SzString *path = (SzString *)env;
  FsResult *r = (FsResult *)sz_alloc(sizeof(FsResult));
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
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Fs.mkdirs: %s: %s", tmp, strerror(errno));
        r->is_err = 1;
        r->as.err = sz_error_new(2, msg);
        goto done;
      }
      *q = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.mkdirs: %s: %s", tmp, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  r->is_err = 0;
  r->as.ok = NULL;
done:
  sz_release(path);
  return r;
}

static SzIo *fs_after_mkdirs(void *value, void *env) {
  if ((intptr_t)value)
    return sz_testrt_fs_mkdirs((SzString *)env);
  return fm_drop(sz_io_delay(fs_mkdirs_result, env), unwrap_fs, NULL);
}

SzIo *sz_fs_mkdirs(SzString *path) {
  if (!path)
    sz_panic("sz_fs_mkdirs(null)");
  return fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_mkdirs, path);
}

static void *fs_canonicalize_result(void *env) {
  SzString *path = (SzString *)env;
  FsResult *r = (FsResult *)sz_alloc(sizeof(FsResult));
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
  sz_release(path);
  return r;
}

static SzIo *fs_after_canonicalize(void *value, void *env) {
  if ((intptr_t)value)
    return sz_testrt_fs_canonicalize((SzString *)env);
  return fm_drop(sz_io_delay(fs_canonicalize_result, env), unwrap_fs, NULL);
}

SzIo *sz_fs_canonicalize(SzString *path) {
  if (!path)
    sz_panic("sz_fs_canonicalize(null)");
  return fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_canonicalize,
                       path);
}
