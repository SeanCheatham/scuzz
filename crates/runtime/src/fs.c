#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "scuzz_rt.h"
#include "rt_util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#include <sys/xattr.h>
#endif

/* Blessed filesystem IO — live interpreter or TestRuntime mem FS.
 * Fake vs live is chosen when the IO runs (after sz_testrt_install in
 * runtime_main), not when the graph is built. */

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
  FILE *f;
  struct stat st;
  sz_timeline_log_cstr("Fs.read", p);
  f = fopen(p, "rb");
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

enum { FS_PATH_MAX = 4096, FS_LINK_HOPS = 40 };

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
static int fs_xattr_ignored(int err) {
  if (err == EPERM || err == EACCES || err == EOPNOTSUPP || err == ENOTSUP ||
      err == EINVAL)
    return 1;
#ifdef ENODATA
  if (err == ENODATA)
    return 1;
#endif
#ifdef ENOATTR
  if (err == ENOATTR)
    return 1;
#endif
  return 0;
}

static ssize_t fs_list_xattr(int fd, char *buf, size_t len) {
#if defined(__APPLE__)
  return flistxattr(fd, buf, len, 0);
#else
  return flistxattr(fd, buf, len);
#endif
}

static ssize_t fs_get_xattr(int fd, const char *name, void *buf, size_t len) {
#if defined(__APPLE__)
  return fgetxattr(fd, name, buf, len, 0, 0);
#else
  return fgetxattr(fd, name, buf, len);
#endif
}

static int fs_set_xattr(int fd, const char *name, const void *buf, size_t len) {
#if defined(__APPLE__)
  return fsetxattr(fd, name, buf, len, 0, 0);
#else
  return fsetxattr(fd, name, buf, len, 0);
#endif
}

static int fs_copy_xattrs(int from_fd, int to_fd) {
  ssize_t list_len;
  char *names;
  char *cursor;
  char *end;
  list_len = fs_list_xattr(from_fd, NULL, 0);
  if (list_len < 0)
    return fs_xattr_ignored(errno) ? 0 : -1;
  if (list_len == 0)
    return 0;
  names = (char *)sz_alloc((size_t)list_len + 1);
  list_len = fs_list_xattr(from_fd, names, (size_t)list_len);
  if (list_len < 0) {
    int err = errno;
    sz_free(names);
    errno = err;
    return fs_xattr_ignored(err) ? 0 : -1;
  }
  names[list_len] = '\0';
  end = names + list_len;
  cursor = names;
  while (cursor < end) {
    size_t name_len = strlen(cursor);
    ssize_t value_len;
    char *value;
    if (name_len == 0) {
      cursor++;
      continue;
    }
    value_len = fs_get_xattr(from_fd, cursor, NULL, 0);
    if (value_len < 0) {
      int err = errno;
      if (fs_xattr_ignored(err)) {
        cursor += name_len + 1;
        continue;
      }
      sz_free(names);
      errno = err;
      return -1;
    }
    value = (char *)sz_alloc((size_t)value_len + 1);
    if (value_len > 0) {
      ssize_t got = fs_get_xattr(from_fd, cursor, value, (size_t)value_len);
      if (got != value_len) {
        int err = errno;
        sz_free(value);
        if (fs_xattr_ignored(err)) {
          cursor += name_len + 1;
          continue;
        }
        sz_free(names);
        errno = err;
        return -1;
      }
    }
    if (fs_set_xattr(to_fd, cursor, value, (size_t)value_len) != 0) {
      int err = errno;
      sz_free(value);
      if (fs_xattr_ignored(err)) {
        cursor += name_len + 1;
        continue;
      }
      sz_free(names);
      errno = err;
      return -1;
    }
    sz_free(value);
    cursor += name_len + 1;
  }
  sz_free(names);
  return 0;
}
#else
static int fs_copy_xattrs(int from_fd, int to_fd) {
  (void)from_fd;
  (void)to_fd;
  return 0;
}
#endif

/* Follow a symbolic link chain. Write the final path that is not a link. */
static int fs_follow_link(const char *path, char *out, size_t out_sz) {
  char current[FS_PATH_MAX];
  int hop;
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof current || out_sz == 0) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(current, path, len + 1);
  for (hop = 0; hop < FS_LINK_HOPS; hop++) {
    struct stat st;
    char link[FS_PATH_MAX];
    ssize_t n;
    char *slash;
    if (lstat(current, &st) != 0)
      return -1;
    if (!S_ISLNK(st.st_mode)) {
      if (strlen(current) >= out_sz) {
        errno = ENAMETOOLONG;
        return -1;
      }
      memcpy(out, current, strlen(current) + 1);
      return 0;
    }
    n = readlink(current, link, sizeof link - 1);
    if (n < 0)
      return -1;
    if (n == 0 || n == (ssize_t)(sizeof link - 1)) {
      errno = n == 0 ? EINVAL : ENAMETOOLONG;
      return -1;
    }
    link[n] = '\0';
    slash = strrchr(current, '/');
    if (link[0] == '/' || !slash) {
      memcpy(current, link, (size_t)n + 1);
    } else {
      char joined[FS_PATH_MAX];
      size_t prefix = (size_t)(slash - current);
      if (prefix + 1 + (size_t)n >= sizeof joined) {
        errno = ENAMETOOLONG;
        return -1;
      }
      memcpy(joined, current, prefix);
      joined[prefix] = '/';
      memcpy(joined + prefix + 1, link, (size_t)n + 1);
      memcpy(current, joined, prefix + 1 + (size_t)n + 1);
    }
  }
  errno = ELOOP;
  return -1;
}

static int fs_sync_parent(const char *file) {
  const char *slash = strrchr(file, '/');
  int dfd;
  int rc;
  if (!slash)
    dfd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  else if (slash == file)
    dfd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  else {
    size_t n = (size_t)(slash - file);
    char *dir = (char *)sz_alloc(n + 1);
    memcpy(dir, file, n);
    dir[n] = '\0';
    dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    sz_free(dir);
  }
  if (dfd < 0)
    return -1;
  rc = fsync(dfd);
  /* A directory sync is not supported on every file system. */
  if (rc != 0 && errno == EINVAL)
    rc = 0;
  {
    int saved = errno;
    if (close(dfd) != 0 && rc == 0)
      return -1;
    if (rc != 0)
      errno = saved;
  }
  return rc;
}

/* Replace one regular file. A symbolic link names the regular file at the
 * end of the link chain. The link stays. Sync the new file and the
 * destination directory before success. A power loss keeps the previous
 * file or the complete new file. Copy access mode. Copy owner, group, and
 * extended attributes when the process can set them. A replacement updates
 * the timestamps. */
static void *fs_write_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = (SzString *)pack->left;
  SzString *contents = (SzString *)pack->right;
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  const char *target = p;
  const char *slash;
  const char suffix[] = ".scuzz-write-XXXXXX";
  char followed[FS_PATH_MAX];
  size_t dir_len;
  struct stat st;
  struct stat meta;
  mode_t mode = 0600;
  char *temporary = NULL;
  FILE *f = NULL;
  int fd, saved_errno, created = 0, renamed = 0, have_old = 0, old_fd = -1;
  sz_timeline_log_cstr("Fs.write", p);
  if (lstat(p, &st) == 0) {
    if (S_ISLNK(st.st_mode)) {
      if (fs_follow_link(p, followed, sizeof followed) != 0)
        goto fail;
      target = followed;
      if (lstat(target, &st) != 0) {
        errno = EINVAL;
        goto fail;
      }
    }
    if (!S_ISREG(st.st_mode)) {
      errno = EINVAL;
      goto fail;
    }
    have_old = 1;
    meta = st;
    mode = st.st_mode & 0777;
    old_fd = open(target, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (old_fd < 0 && errno != EACCES)
      goto fail;
  } else if (errno != ENOENT) {
    goto fail;
  }
  slash = strrchr(target, '/');
  dir_len = slash ? (size_t)(slash - target) + 1 : 0;
  temporary = (char *)sz_alloc(dir_len + sizeof(suffix));
  memcpy(temporary, target, dir_len);
  memcpy(temporary + dir_len, suffix, sizeof(suffix));
  fd = mkstemp(temporary);
  if (fd < 0)
    goto fail;
  created = 1;
  f = fdopen(fd, "wb");
  if (!f) {
    saved_errno = errno;
    close(fd);
    errno = saved_errno;
    goto fail;
  }
  if (contents && contents->len &&
      fwrite(contents->data, 1, contents->len, f) != contents->len) {
    if (!errno)
      errno = EIO;
    goto fail;
  }
  if (fflush(f) != 0)
    goto fail;
  if (fchmod(fileno(f), mode) != 0)
    goto fail;
  if (have_old) {
    if (fchown(fileno(f), meta.st_uid, meta.st_gid) != 0 && errno != EPERM)
      goto fail;
    if (old_fd >= 0 && fs_copy_xattrs(old_fd, fileno(f)) != 0)
      goto fail;
  }
  if (fsync(fileno(f)) != 0)
    goto fail;
  if (fclose(f) != 0) {
    f = NULL;
    goto fail;
  }
  f = NULL;
  if (rename(temporary, target) != 0)
    goto fail;
  renamed = 1;
  if (fs_sync_parent(target) != 0)
    goto fail;
  if (old_fd >= 0)
    close(old_fd);
  sz_free(temporary);
  return r;
fail:
  saved_errno = errno;
  if (f)
    fclose(f);
  if (old_fd >= 0)
    close(old_fd);
  if (temporary) {
    if (created && !renamed)
      unlink(temporary);
    sz_free(temporary);
  }
  {
    char msg[512];
    snprintf(msg, sizeof(msg), "Fs.write: cannot replace %s: %s", p,
             strerror(saved_errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
  }
  return r;
}

static SzIo *fs_after_write(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = (SzString *)pack->left;
  const char *p = sz_string_cstr(path);
  size_t len = (size_t)sz_string_len(path);
  if (!len || strlen(p) != len || p[len - 1] == '/')
    return sz_io_fail_cstr("Fs.write: invalid file path");
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

static int fs_join_into(char *out, size_t out_sz, const char *dir, const char *name) {
  int n;
  size_t dlen = strlen(dir);
  if (dlen == 1 && dir[0] == '/')
    n = snprintf(out, out_sz, "/%s", name);
  else if (dlen > 0 && dir[dlen - 1] == '/')
    n = snprintf(out, out_sz, "%s%s", dir, name);
  else if (dlen == 0)
    n = snprintf(out, out_sz, "%s", name);
  else
    n = snprintf(out, out_sz, "%s/%s", dir, name);
  return n >= 0 && (size_t)n < out_sz;
}

static int fs_name_is_dir(const char *dir, const char *name) {
  char full[2048];
  struct stat st;
  if (!fs_join_into(full, sizeof full, dir, name))
    return 0;
  if (lstat(full, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

static void *fs_list_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  DIR *d;
  sz_timeline_log_cstr("Fs.list", p);
  d = opendir(p);
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
  sz_timeline_log_cstr("Fs.mkdirs", p);
  if (len == 0 || strcmp(p, ".") == 0 || strcmp(p, "/") == 0) {
    r->is_err = 0;
    r->as.ok = NULL;
    goto done;
  }
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
  char *resolved;
  sz_timeline_log_cstr("Fs.canonicalize", p);
  resolved = realpath(p, NULL);
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
  sz_timeline_log_cstr("Fs.exists", p);
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

/* Collapse duplicate slashes, trailing slashes, and "." segments.
 * Keep ".." segments. Writes a NUL-terminated path into out. */
static int fs_collapse_dot_slash(const char *p, char *out, size_t out_sz) {
  size_t i = 0;
  size_t o = 0;
  int abs = 0;
  if (!p)
    p = "";
  if (p[0] == '/') {
    abs = 1;
    if (out_sz < 2)
      return 0;
    out[o++] = '/';
    while (p[i] == '/')
      i++;
  }
  while (p[i]) {
    size_t start = i;
    size_t n;
    while (p[i] && p[i] != '/')
      i++;
    n = i - start;
    if (n == 1 && p[start] == '.') {
      /* skip "." */
    } else if (n > 0) {
      if (o > 0 && out[o - 1] != '/') {
        if (o + 1 >= out_sz)
          return 0;
        out[o++] = '/';
      }
      if (o + n >= out_sz)
        return 0;
      memcpy(out + o, p + start, n);
      o += n;
    }
    while (p[i] == '/')
      i++;
  }
  if (o > 1 && out[o - 1] == '/')
    o--;
  out[o] = '\0';
  if (!abs && o == 0) {
    if (out_sz < 2)
      return 0;
    out[0] = '.';
    out[1] = '\0';
  }
  return 1;
}

static int fs_same_inode(const char *a, const char *b) {
  struct stat sa;
  struct stat sb;
  if (lstat(a, &sa) != 0 || lstat(b, &sb) != 0)
    return 0;
  return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int fs_is_root_path(const char *p) {
  char collapsed[2048];
  if (!p || !p[0])
    return 1;
  if (!fs_collapse_dot_slash(p, collapsed, sizeof collapsed))
    return 0;
  if (strcmp(collapsed, ".") == 0 || strcmp(collapsed, "/") == 0 ||
      strcmp(collapsed, "..") == 0)
    return 1;
  return fs_same_inode(p, "/") || fs_same_inode(p, ".");
}

static int fs_parent_is_dir(const char *path) {
  const char *slash = strrchr(path, '/');
  char buf[2048];
  size_t n;
  struct stat st;
  if (!slash)
    return 1;
  if (slash == path) {
    return stat("/", &st) == 0 && S_ISDIR(st.st_mode);
  }
  n = (size_t)(slash - path);
  if (n >= sizeof buf)
    return 0;
  memcpy(buf, path, n);
  buf[n] = '\0';
  return stat(buf, &st) == 0 && S_ISDIR(st.st_mode);
}

static int fs_rm_tree(const char *path, int depth, FsResult *r) {
  struct stat st;
  if (depth > 256) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.delete: path too deep");
    return 0;
  }
  if (lstat(path, &st) != 0) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.delete: %s: %s", path, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    return 0;
  }
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    struct dirent *ent;
    if (!d) {
      char msg[512];
      snprintf(msg, sizeof msg, "Fs.delete: cannot open %s: %s", path,
               strerror(errno));
      r->is_err = 1;
      r->as.err = sz_error_new(2, msg);
      return 0;
    }
    while ((ent = readdir(d)) != NULL) {
      char child[2048];
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      if (!fs_join_into(child, sizeof child, path, ent->d_name)) {
        closedir(d);
        r->is_err = 1;
        r->as.err = sz_error_new(2, "Fs.delete: path too long");
        return 0;
      }
      if (!fs_rm_tree(child, depth + 1, r)) {
        closedir(d);
        return 0;
      }
    }
    closedir(d);
    if (rmdir(path) != 0) {
      char msg[512];
      snprintf(msg, sizeof msg, "Fs.delete: rmdir %s: %s", path, strerror(errno));
      r->is_err = 1;
      r->as.err = sz_error_new(2, msg);
      return 0;
    }
    return 1;
  }
  if (unlink(path) != 0) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.delete: unlink %s: %s", path, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    return 0;
  }
  return 1;
}

static void *fs_delete_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  sz_timeline_log_cstr("Fs.delete", p);
  if (fs_is_root_path(p)) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.delete: refused root");
    goto done;
  }
  if (!fs_rm_tree(p, 0, r))
    goto done;
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

static SzIo *fs_after_delete(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_delete(pack_path(pack));
  return fm_drop(sz_io_delay(fs_delete_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_delete(SzString *path) { return fs_bind(path, fs_after_delete); }

static void *fs_rename_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *from = pack ? (SzString *)pack->left : NULL;
  SzString *to = pack ? (SzString *)pack->right : NULL;
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *src = sz_string_cstr(from);
  const char *dst = sz_string_cstr(to);
  struct stat st;
  sz_timeline_log_cstr("Fs.rename", src);
  if (fs_is_root_path(src) || fs_is_root_path(dst)) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: refused root");
    goto done;
  }
  if (lstat(src, &st) != 0) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.rename: %s: %s", src, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  if (lstat(dst, &st) == 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: dest exists");
    goto done;
  }
  if (!fs_parent_is_dir(dst)) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.rename: no parent");
    goto done;
  }
  if (rename(src, dst) != 0) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.rename: %s: %s", src, strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  r->is_err = 0;
  r->as.ok = NULL;
done:
  return r;
}

static SzIo *fs_after_rename(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_rename((SzString *)pack->left, (SzString *)pack->right);
  return fm_drop(sz_io_delay(fs_rename_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_rename(SzString *from, SzString *to) {
  SzPair *pack;
  if (!from || !to)
    sz_panic("sz_fs_rename(null)");
  pack = sz_pair_new(from, to);
  {
    SzIo *io = fm_drop(sz_io_delay(fs_dispatch, NULL), fs_after_rename, pack);
    sz_release(pack);
    return io;
  }
}

static int fs_walk_into(const char *root, const char *rel, int depth, SzList **acc,
                        FsResult *r) {
  char full[2048];
  DIR *d;
  struct dirent *ent;
  if (depth > 256) {
    r->is_err = 1;
    r->as.err = sz_error_new(2, "Fs.walk: path too deep");
    return 0;
  }
  if (rel[0]) {
    if (!fs_join_into(full, sizeof full, root, rel)) {
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.walk: path too long");
      return 0;
    }
  } else {
    if (strlen(root) >= sizeof full) {
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.walk: path too long");
      return 0;
    }
    memcpy(full, root, strlen(root) + 1);
  }
  d = opendir(full);
  if (!d) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.walk: cannot open %s: %s", full,
             strerror(errno));
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    return 0;
  }
  while ((ent = readdir(d)) != NULL) {
    char child_rel[2048];
    char child_full[2048];
    struct stat st;
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;
    if (rel[0]) {
      if (!fs_join_into(child_rel, sizeof child_rel, rel, ent->d_name)) {
        closedir(d);
        r->is_err = 1;
        r->as.err = sz_error_new(2, "Fs.walk: path too long");
        return 0;
      }
    } else {
      if (strlen(ent->d_name) >= sizeof child_rel) {
        closedir(d);
        r->is_err = 1;
        r->as.err = sz_error_new(2, "Fs.walk: path too long");
        return 0;
      }
      memcpy(child_rel, ent->d_name, strlen(ent->d_name) + 1);
    }
    if (!fs_join_into(child_full, sizeof child_full, full, ent->d_name)) {
      closedir(d);
      r->is_err = 1;
      r->as.err = sz_error_new(2, "Fs.walk: path too long");
      return 0;
    }
    if (lstat(child_full, &st) != 0)
      continue;
    *acc = cons_fs_entry(*acc, child_rel, S_ISDIR(st.st_mode) ? 1 : 0);
    if (S_ISDIR(st.st_mode)) {
      if (!fs_walk_into(root, child_rel, depth + 1, acc, r)) {
        closedir(d);
        return 0;
      }
    }
  }
  closedir(d);
  return 1;
}

static void *fs_walk_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *path = pack_path(pack);
  FsResult *r = (FsResult *)rc_box_zero(sizeof(FsResult));
  const char *p = sz_string_cstr(path);
  struct stat st;
  SzList *acc;
  sz_timeline_log_cstr("Fs.walk", p);
  if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
    char msg[512];
    snprintf(msg, sizeof msg, "Fs.walk: not a directory: %s", p);
    r->is_err = 1;
    r->as.err = sz_error_new(2, msg);
    goto done;
  }
  acc = sz_list_nil();
  if (!fs_walk_into(p, "", 0, &acc, r)) {
    sz_release(acc);
    goto done;
  }
  r->is_err = 0;
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    r->as.ok = rev;
  }
done:
  return r;
}

static SzIo *fs_after_walk(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  if ((intptr_t)value)
    return sz_testrt_fs_walk(pack_path(pack));
  return fm_drop(sz_io_delay(fs_walk_result, pack), unwrap_fs, NULL);
}

SzIo *sz_fs_walk(SzString *path) { return fs_bind(path, fs_after_walk); }

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
