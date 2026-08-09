#define _POSIX_C_SOURCE 200809L
#include "scalui_rt.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Blessed filesystem IO — live interpreter (fake FS is Phase 6). */

typedef struct {
  int is_err;
  union {
    SuError *err;
    void *ok;
  } as;
} FsResult;

typedef struct {
  SuString *path;
  SuString *contents;
} FsWriteEnv;

static SuIo *unwrap_fs(void *value, void *env) {
  (void)env;
  FsResult *r = (FsResult *)value;
  if (!r)
    return su_io_fail_cstr("Fs: null result");
  if (r->is_err)
    return su_io_fail(r->as.err);
  return su_io_pure(r->as.ok);
}

static void *fs_read_result(void *env) {
  SuString *path = (SuString *)env;
  FsResult *r = (FsResult *)su_alloc(sizeof(FsResult));
  const char *p = su_string_cstr(path);
  FILE *f = fopen(p, "rb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.read: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = su_error_new(2, msg);
    return r;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.read: seek end failed");
    return r;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.read: ftell failed");
    return r;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.read: seek set failed");
    return r;
  }
  char *buf = (char *)su_alloc((size_t)sz + 1);
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = '\0';
  SuString *s = su_string_from_bytes(buf, n);
  su_free(buf);
  r->is_err = 0;
  r->as.ok = s;
  return r;
}

SuIo *su_fs_read(SuString *path) {
  if (!path)
    su_panic("su_fs_read(null)");
  return su_io_flatmap(su_io_delay(fs_read_result, path), unwrap_fs, NULL);
}

static void *fs_write_result(void *env) {
  FsWriteEnv *e = (FsWriteEnv *)env;
  FsResult *r = (FsResult *)su_alloc(sizeof(FsResult));
  const char *p = su_string_cstr(e->path);
  FILE *f = fopen(p, "wb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.write: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = su_error_new(2, msg);
    return r;
  }
  SuString *c = e->contents;
  if (c && c->len) {
    if (fwrite(c->data, 1, c->len, f) != c->len) {
      fclose(f);
      r->is_err = 1;
      r->as.err = su_error_new(2, "Fs.write: short write");
      return r;
    }
  }
  fclose(f);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

SuIo *su_fs_write(SuString *path, SuString *contents) {
  if (!path)
    su_panic("su_fs_write(null path)");
  FsWriteEnv *e = (FsWriteEnv *)su_alloc(sizeof(FsWriteEnv));
  e->path = path;
  e->contents = contents ? contents : su_string_from_cstr("");
  return su_io_flatmap(su_io_delay(fs_write_result, e), unwrap_fs, NULL);
}

static void *fs_list_result(void *env) {
  SuString *path = (SuString *)env;
  FsResult *r = (FsResult *)su_alloc(sizeof(FsResult));
  const char *p = su_string_cstr(path);
  DIR *d = opendir(p);
  if (!d) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.list: cannot open %s: %s", p, strerror(errno));
    r->is_err = 1;
    r->as.err = su_error_new(2, msg);
    return r;
  }
  SuList *acc = su_list_nil();
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    acc = su_list_cons(su_string_from_cstr(ent->d_name), acc);
  }
  closedir(d);
  r->is_err = 0;
  r->as.ok = su_list_reverse(acc);
  return r;
}

SuIo *su_fs_list(SuString *path) {
  if (!path)
    su_panic("su_fs_list(null)");
  return su_io_flatmap(su_io_delay(fs_list_result, path), unwrap_fs, NULL);
}

static void *fs_mkdirs_result(void *env) {
  SuString *path = (SuString *)env;
  FsResult *r = (FsResult *)su_alloc(sizeof(FsResult));
  const char *p = su_string_cstr(path);
  char tmp[1024];
  size_t len = strlen(p);
  if (len >= sizeof(tmp)) {
    r->is_err = 1;
    r->as.err = su_error_new(2, "Fs.mkdirs: path too long");
    return r;
  }
  memcpy(tmp, p, len + 1);
  for (char *q = tmp + 1; *q; q++) {
    if (*q == '/') {
      *q = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Fs.mkdirs: %s: %s", tmp, strerror(errno));
        r->is_err = 1;
        r->as.err = su_error_new(2, msg);
        return r;
      }
      *q = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.mkdirs: %s: %s", tmp, strerror(errno));
    r->is_err = 1;
    r->as.err = su_error_new(2, msg);
    return r;
  }
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

SuIo *su_fs_mkdirs(SuString *path) {
  if (!path)
    su_panic("su_fs_mkdirs(null)");
  return su_io_flatmap(su_io_delay(fs_mkdirs_result, path), unwrap_fs, NULL);
}
