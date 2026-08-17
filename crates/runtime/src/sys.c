#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Process / args / console kit (Sys.args, IO.println, clang link).
 * TestRuntime rejects Sys.exec / Sys.spawn so sim cannot fork a child. */

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

static char **g_argv = NULL;
static int g_argc = 0;

void sz_sys_set_args(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
}

static void *sys_args_thunk(void *env) {
  (void)env;
  if (sz_testrt_sys_has_args_override())
    return sz_testrt_sys_args_list();
  {
    SzList *acc = sz_list_nil();
    /* Skip argv[0] (program name); expose user args only. */
    for (int i = g_argc - 1; i >= 1; i--) {
      SzString *s = sz_string_from_cstr(g_argv[i] ? g_argv[i] : "");
      SzList *old = acc;
      acc = sz_list_cons(s, old);
      sz_release(s);
      sz_release(old);
    }
    return acc;
  }
}

SzIo *sz_sys_args(void) {
  return sz_io_delay(sys_args_thunk, NULL);
}

typedef struct {
  int is_err;
  int retry;
  union {
    SzError *err;
    void *ok;
  } as;
} SysResult;

static SzIo *unwrap_sys(void *value, void *env) {
  (void)env;
  SysResult *r = (SysResult *)value;
  if (!r)
    return sz_io_fail_cstr("Sys: null result");
  if (r->is_err) {
    SzError *err = r->as.err;
    r->as.err = NULL;
    sz_free(r);
    return fail_drop(err);
  }
  {
    void *ok = r->as.ok;
    r->as.ok = NULL;
    sz_free(r);
    return pure_drop(ok);
  }
}

static char *g_inbuf = NULL;
static size_t g_inlen = 0;
static size_t g_incap = 0;

static int inbuf_grow(size_t need) {
  char *nbuf;
  size_t cap = g_incap ? g_incap : 256;
  while (cap < need)
    cap *= 2;
  nbuf = (char *)sz_alloc(cap);
  if (g_inlen)
    memcpy(nbuf, g_inbuf, g_inlen);
  sz_free(g_inbuf);
  g_inbuf = nbuf;
  g_incap = cap;
  return 0;
}

static int inbuf_take_line(SysResult *r) {
  size_t i;
  size_t n;
  for (i = 0; i < g_inlen; i++) {
    if (g_inbuf[i] != '\n')
      continue;
    n = i;
    if (n > 0 && g_inbuf[n - 1] == '\r')
      n--;
    r->is_err = 0;
    r->as.ok = sz_string_from_bytes(g_inbuf, n);
    i++;
    if (i < g_inlen)
      memmove(g_inbuf, g_inbuf + i, g_inlen - i);
    g_inlen -= i;
    return 1;
  }
  return 0;
}

static void inbuf_take_eof(SysResult *r) {
  size_t n = g_inlen;
  r->is_err = 0;
  if (n > 0 && g_inbuf[n - 1] == '\r')
    n--;
  r->as.ok = sz_string_from_bytes(g_inbuf ? g_inbuf : "", n);
  g_inlen = 0;
}

static void *sys_read_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_sys_is_fake() ? 1 : 0);
}

static void *sys_try_line(void *env) {
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  (void)env;
  if (inbuf_take_line(r))
    return r;
  r->retry = 1;
  return r;
}

static void *sys_read_more(void *env) {
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  char tmp[256];
  ssize_t n;
  (void)env;
  n = read(STDIN_FILENO, tmp, sizeof tmp);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.readLine: read failed");
    return r;
  }
  if (n == 0) {
    inbuf_take_eof(r);
    return r;
  }
  if (g_inlen + (size_t)n > g_incap)
    inbuf_grow(g_inlen + (size_t)n);
  memcpy(g_inbuf + g_inlen, tmp, (size_t)n);
  g_inlen += (size_t)n;
  if (inbuf_take_line(r))
    return r;
  r->retry = 1;
  return r;
}

static SzIo *sys_poll_line(void *value, void *env);

static SzIo *sys_unwrap_line(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  (void)env;
  if (r && r->retry) {
    sz_free(r);
    return sys_poll_line(NULL, NULL);
  }
  return unwrap_sys(value, NULL);
}

static SzIo *sys_after_poll(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_io_delay(sys_read_more, NULL), sys_unwrap_line, NULL);
}

static SzIo *sys_poll_line(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_io_poll_readable(STDIN_FILENO), sys_after_poll, NULL);
}

static SzIo *sys_after_try(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  (void)env;
  if (r && r->retry) {
    sz_free(r);
    return sys_poll_line(NULL, NULL);
  }
  return unwrap_sys(value, NULL);
}

static SzIo *sys_after_dispatch(void *value, void *env) {
  (void)env;
  if ((intptr_t)value)
    return sz_testrt_sys_read_line();
  return fm_drop(sz_io_delay(sys_try_line, NULL), sys_after_try, NULL);
}

SzIo *sz_sys_read_line(void) {
  return fm_drop(sz_io_delay(sys_read_dispatch, NULL), sys_after_dispatch,
                       NULL);
}

static int inbuf_take_n(SysResult *r, size_t n) {
  if (g_inlen < n)
    return 0;
  r->is_err = 0;
  r->as.ok = sz_string_from_bytes(g_inbuf ? g_inbuf : "", n);
  if (n < g_inlen)
    memmove(g_inbuf, g_inbuf + n, g_inlen - n);
  g_inlen -= n;
  return 1;
}

static void *sys_try_n(void *env) {
  size_t n = (size_t) * (int64_t *)env;
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  if (inbuf_take_n(r, n))
    return r;
  r->retry = 1;
  return r;
}

static void *sys_read_more_n(void *env) {
  size_t want = (size_t) * (int64_t *)env;
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  char tmp[256];
  ssize_t n;
  n = read(STDIN_FILENO, tmp, sizeof tmp);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    r->retry = 1;
    return r;
  }
  if (n < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.read: read failed");
    return r;
  }
  if (n == 0) {
    size_t take = g_inlen < want ? g_inlen : want;
    r->is_err = 0;
    r->as.ok = sz_string_from_bytes(g_inbuf ? g_inbuf : "", take);
    if (take < g_inlen)
      memmove(g_inbuf, g_inbuf + take, g_inlen - take);
    g_inlen -= take;
    return r;
  }
  if (g_inlen + (size_t)n > g_incap)
    inbuf_grow(g_inlen + (size_t)n);
  memcpy(g_inbuf + g_inlen, tmp, (size_t)n);
  g_inlen += (size_t)n;
  if (inbuf_take_n(r, want))
    return r;
  r->retry = 1;
  return r;
}

static SzIo *sys_poll_n(void *value, void *env);

static SzIo *sys_unwrap_n(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return sys_poll_n(NULL, env);
  }
  sz_free(env);
  return unwrap_sys(value, NULL);
}

static SzIo *sys_after_poll_n(void *value, void *env) {
  (void)value;
  return fm_drop(sz_io_delay(sys_read_more_n, env), sys_unwrap_n, env);
}

static SzIo *sys_poll_n(void *value, void *env) {
  (void)value;
  return fm_drop(sz_io_poll_readable(STDIN_FILENO), sys_after_poll_n, env);
}

static SzIo *sys_after_try_n(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  if (r && r->retry) {
    sz_free(r);
    return sys_poll_n(NULL, env);
  }
  sz_free(env);
  return unwrap_sys(value, NULL);
}

static SzIo *sys_after_dispatch_n(void *value, void *env) {
  int64_t n = *(int64_t *)env;
  if ((intptr_t)value) {
    sz_free(env);
    return sz_testrt_sys_read(n);
  }
  if (n <= 0) {
    SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
    r->as.ok = sz_string_from_cstr("");
    sz_free(env);
    return unwrap_sys(r, NULL);
  }
  return fm_drop(sz_io_delay(sys_try_n, env), sys_after_try_n, env);
}

SzIo *sz_sys_read(int64_t n) {
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = n < 0 ? 0 : n;
  return fm_drop(sz_io_delay(sys_read_dispatch, NULL), sys_after_dispatch_n,
                       p);
}

static void *sys_write_result(void *env) {
  SzString *s = (SzString *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  const char *p = s ? sz_string_cstr(s) : "";
  size_t n = s ? s->len : 0;
  r->is_err = 0;
  r->as.ok = NULL;
  if (sz_testrt_sys_is_fake())
    sz_testrt_stdout_write(p, n);
  if (n > 0 && fwrite(p, 1, n, stdout) != n) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.write: write failed");
  } else if (fflush(stdout) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.write: flush failed");
  }
  sz_release(s);
  return r;
}

SzIo *sz_sys_write(SzString *s) {
  if (!s)
    sz_panic("sz_sys_write(null)");
  return fm_drop(sz_io_delay(sys_write_result, s), unwrap_sys, NULL);
}

typedef struct ExecSt {
  SzString *cmd;
  int read_fd;
  pid_t pid;
} ExecSt;

static void exec_free(ExecSt *st) {
  int status = 0;
  if (!st)
    return;
  if (st->read_fd >= 0) {
    close(st->read_fd);
    st->read_fd = -1;
  }
  if (st->pid > 0)
    (void)waitpid(st->pid, &status, WNOHANG);
  sz_release(st->cmd);
  st->cmd = NULL;
}

static void *sys_exec_start(void *env) {
  ExecSt *st = (ExecSt *)env;
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  int fds[2];
  pid_t pid;
  const char *c = sz_string_cstr(st->cmd);

  if (pipe(fds) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: pipe failed");
    return r;
  }
  pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: fork failed");
    return r;
  }
  if (pid == 0) {
    close(fds[0]);
    execl("/bin/sh", "sh", "-c", c, (char *)NULL);
    _exit(127);
  }
  close(fds[1]);
  (void)fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  st->read_fd = fds[0];
  st->pid = pid;
  r->is_err = 0;
  return r;
}

static void *sys_exec_reap(void *env) {
  ExecSt *st = (ExecSt *)env;
  SysResult *r = (SysResult *)sz_alloc_zero(sizeof(SysResult));
  int status = 0;
  int code;
  pid_t w;

  do {
    w = waitpid(st->pid, &status, 0);
  } while (w < 0 && errno == EINTR);
  if (st->read_fd >= 0) {
    close(st->read_fd);
    st->read_fd = -1;
  }
  st->pid = 0;
  if (w < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: wait failed");
    return r;
  }
  code = status;
#ifdef WIFEXITED
  if (WIFEXITED(status))
    code = WEXITSTATUS(status);
#endif
  r->is_err = 0;
  r->as.ok = sz_box_i64((int64_t)code);
  return r;
}

static SzIo *exec_after_poll(void *value, void *env) {
  (void)value;
  return fm_drop(sz_io_delay(sys_exec_reap, env), unwrap_sys, NULL);
}

static SzIo *exec_finish(void *code, void *env) {
  exec_free((ExecSt *)env);
  return pure_drop(code);
}

static SzIo *exec_on_err(SzError *err, void *env) {
  exec_free((ExecSt *)env);
  return fail_drop(err);
}

static SzIo *exec_after_start(void *value, void *env) {
  ExecSt *st = (ExecSt *)env;
  SysResult *r = (SysResult *)value;
  SzIo *io;
  if (!r || r->is_err)
    return unwrap_sys(value, NULL);
  sz_free(r);
  io = fm_drop(sz_io_poll_readable(st->read_fd), exec_after_poll, st);
  return fm_drop(io, exec_finish, st);
}

static SzIo *exec_keep_pair(void *value, void *env) {
  (void)value;
  (void)env;
  return pure_drop(NULL);
}

static SzIo *exec_after_kick(void *ignored, void *env) {
  SzPair *p = (SzPair *)env;
  ExecSt *st = (ExecSt *)sz_rc_alloc(sizeof(ExecSt), SZ_RC_BOX);
  SzIo *io;
  (void)ignored;
  memset(st, 0, sizeof(ExecSt));
  sz_retain(p->left);
  st->cmd = (SzString *)p->left;
  st->read_fd = -1;
  io = fm_drop(sz_io_delay(sys_exec_start, st), exec_after_start, st);
  {
    SzIo *handled = sz_io_handle_error_with(io, exec_on_err, st);
    sz_release(io);
    sz_release(st);
    return handled;
  }
}

SzIo *sz_sys_exec(SzString *cmd) {
  SzPair *p;
  if (!cmd)
    sz_panic("sz_sys_exec(null)");
  p = sz_pair_new(cmd, NULL);
  if (sz_testrt_sys_is_fake()) {
    SzIo *io = fm_drop(sz_io_fail_cstr("Sys.exec: rejected under TestRuntime"),
                       exec_keep_pair, p);
    sz_release(p);
    return io;
  }
  {
    SzIo *io = fm_drop(sz_io_pure(NULL), exec_after_kick, p);
    sz_release(p);
    return io;
  }
}

static void *sys_spawn_result(void *env) {
  SzString *cmd = (SzString *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  pid_t pid;
  const char *c = sz_string_cstr(cmd);
  pid = fork();
  if (pid < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.spawn: fork failed");
    sz_release(cmd);
    return r;
  }
  if (pid == 0) {
    execl("/bin/sh", "sh", "-c", c, (char *)NULL);
    _exit(127);
  }
  r->is_err = 0;
  r->as.ok = sz_box_i64((int64_t)pid);
  sz_release(cmd);
  return r;
}

SzIo *sz_sys_spawn(SzString *cmd) {
  if (!cmd)
    sz_panic("sz_sys_spawn(null)");
  if (sz_testrt_sys_is_fake())
    return sz_io_fail_cstr("Sys.spawn: rejected under TestRuntime");
  return fm_drop(sz_io_delay(sys_spawn_result, cmd), unwrap_sys, NULL);
}

static void *sys_alive_result(void *env) {
  int64_t pid = *(int64_t *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  int status = 0;
  pid_t w;
  sz_free(env);
  r->is_err = 0;
  if (sz_testrt_sys_is_fake()) {
    r->as.ok = sz_box_i64(sz_testrt_proc_alive(pid));
    return r;
  }
  w = waitpid((pid_t)pid, &status, WNOHANG);
  if (w == 0)
    r->as.ok = sz_box_i64(1);
  else
    r->as.ok = sz_box_i64(0);
  return r;
}

SzIo *sz_sys_alive(int64_t pid) {
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = pid;
  return fm_drop(sz_io_delay(sys_alive_result, p), unwrap_sys, NULL);
}

static void *sys_kill_result(void *env) {
  int64_t pid = *(int64_t *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  sz_free(env);
  r->is_err = 0;
  r->as.ok = NULL;
  if (sz_testrt_sys_is_fake()) {
    sz_testrt_proc_kill(pid);
    return r;
  }
  if (pid > 0 && kill((pid_t)pid, SIGTERM) != 0 && errno != ESRCH) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.kill: kill failed");
  }
  return r;
}

SzIo *sz_sys_kill(int64_t pid) {
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = pid;
  return fm_drop(sz_io_delay(sys_kill_result, p), unwrap_sys, NULL);
}

static void *sys_getenv_result(void *env) {
  SzPair *p = (SzPair *)env;
  SzString *key = p ? (SzString *)p->left : NULL;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  const char *v;
  if (sz_testrt_sys_is_fake())
    v = sz_testrt_env_get(key ? sz_string_cstr(key) : "");
  else
    v = getenv(key ? sz_string_cstr(key) : "");
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(v ? v : "");
  sz_release(p);
  return r;
}

SzIo *sz_sys_getenv(SzString *key) {
  SzPair *p;
  if (!key)
    sz_panic("sz_sys_getenv(null)");
  p = sz_pair_new(key, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(sys_getenv_result, p), unwrap_sys, NULL);
    sz_release(p);
    return io;
  }
}
