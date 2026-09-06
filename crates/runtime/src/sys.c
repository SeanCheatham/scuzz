#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"
#include "rt_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

/* Process / args / console kit (Sys.args, IO.println, clang link).
 * TestRuntime rejects Sys.exec / Sys.spawn so sim cannot fork a child. */

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

/* DELAY wraps `r` in PURE (extra retain at run). Drop that retain here. */
static SzIo *unwrap_sys(void *value, void *env) {
  (void)env;
  SysResult *r = (SysResult *)value;
  if (!r)
    return sz_io_fail_cstr("Sys: null result");
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

static void *sys_fake_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_sys_is_fake() ? 1 : 0);
}

static void *sys_try_line(void *env) {
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  (void)env;
  if (inbuf_take_line(r))
    return r;
  r->retry = 1;
  return r;
}

static void *sys_read_more(void *env) {
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
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
    sz_release(r);
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
    sz_release(r);
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
  return fm_drop(sz_io_delay(sys_fake_dispatch, NULL), sys_after_dispatch,
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
  size_t n = (size_t)sz_unbox_i64(env);
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  if (inbuf_take_n(r, n))
    return r;
  r->retry = 1;
  return r;
}

static void *sys_read_more_n(void *env) {
  size_t want = (size_t)sz_unbox_i64(env);
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
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
    sz_release(r);
    return sys_poll_n(NULL, env);
  }
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
    sz_release(r);
    return sys_poll_n(NULL, env);
  }
  return unwrap_sys(value, NULL);
}

static SzIo *sys_after_dispatch_n(void *value, void *env) {
  int64_t n = sz_unbox_i64(env);
  if ((intptr_t)value)
    return sz_testrt_sys_read(n);
  if (n <= 0) {
    SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
    r->as.ok = sz_string_from_cstr("");
    return unwrap_sys(r, NULL);
  }
  return fm_drop(sz_io_delay(sys_try_n, env), sys_after_try_n, env);
}

SzIo *sz_sys_read(int64_t n) {
  void *nbox = sz_box_i64(n < 0 ? 0 : n);
  SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL), sys_after_dispatch_n,
                     nbox);
  sz_release(nbox);
  return io;
}

static void *sys_write_result(void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *s = pack ? (SzString *)pack->left : NULL;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  const char *p = s ? sz_string_cstr(s) : "";
  size_t n = s ? s->len : 0;
  r->is_err = 0;
  r->as.ok = NULL;
  if (sz_testrt_sys_is_fake())
    sz_testrt_stdout_write(p, n);
  else if (n > 0 && fwrite(p, 1, n, stdout) != n) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.write: write failed");
  } else if (fflush(stdout) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.write: flush failed");
  }
  return r;
}

SzIo *sz_sys_write(SzString *s) {
  SzPair *pack;
  if (!s)
    sz_panic("sz_sys_write(null)");
  pack = sz_pair_new(s, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(sys_write_result, pack), unwrap_sys, NULL);
    sz_release(pack);
    return io;
  }
}

#define EXEC_CAP (1024 * 1024)
#define EXEC_FD_MAX 256

typedef struct ExecSt {
  SzString *cmd;
  int out_fd;
  int err_fd;
  pid_t pid;
  int status;
  int overflow;
  char *out_buf;
  size_t out_len;
  size_t out_cap;
  char *err_buf;
  size_t err_len;
  size_t err_cap;
} ExecSt;

static void exec_close_fd(int *fd) {
  if (fd && *fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

static void exec_close_extra_fds(void) {
  int fd;
  for (fd = 3; fd < EXEC_FD_MAX; fd++)
    (void)close(fd);
}

static void exec_reap_pid(pid_t pid) {
  int status = 0;
  pid_t w;
  if (pid <= 0)
    return;
  (void)kill(pid, SIGKILL);
  do {
    w = waitpid(pid, &status, 0);
  } while (w < 0 && errno == EINTR);
}

static void exec_free(ExecSt *st) {
  if (!st)
    return;
  exec_close_fd(&st->out_fd);
  exec_close_fd(&st->err_fd);
  if (st->pid > 0) {
    exec_reap_pid(st->pid);
    st->pid = 0;
  }
  sz_free(st->out_buf);
  sz_free(st->err_buf);
  st->out_buf = NULL;
  st->err_buf = NULL;
  sz_release(st->cmd);
  st->cmd = NULL;
}

static void *exec_cleanup(void *env) {
  exec_free((ExecSt *)env);
  return NULL;
}

static void exec_set_cloexec_nb(int fd) {
  int fl;
  (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
  fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0)
    (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int exec_append(char **buf, size_t *len, size_t *cap, const char *src,
                       size_t n, int *overflow) {
  if (*len >= EXEC_CAP || n > EXEC_CAP - *len) {
    if (overflow) {
      *overflow = 1;
      return 0;
    }
    return 1;
  }
  if (!n)
    return 1;
  if (*len + n + 1 > *cap) {
    size_t nc = *cap ? *cap * 2 : 256;
    char *p;
    while (nc < *len + n + 1)
      nc *= 2;
    if (nc > EXEC_CAP + 1)
      nc = EXEC_CAP + 1;
    p = (char *)sz_alloc(nc);
    if (*buf && *len)
      memcpy(p, *buf, *len);
    sz_free(*buf);
    *buf = p;
    *cap = nc;
  }
  memcpy(*buf + *len, src, n);
  *len += n;
  (*buf)[*len] = '\0';
  return 1;
}

static int exec_drain_fd(int *fd, char **buf, size_t *len, size_t *cap,
                         int *overflow) {
  char tmp[4096];
  if (!fd || *fd < 0)
    return 1;
  for (;;) {
    ssize_t n = read(*fd, tmp, sizeof tmp);
    if (n > 0) {
      if (!exec_append(buf, len, cap, tmp, (size_t)n, overflow))
        return 0;
      continue;
    }
    if (n == 0) {
      exec_close_fd(fd);
      return 1;
    }
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 1;
    return 0;
  }
}

static void *sys_exec_start(void *env) {
  ExecSt *st = (ExecSt *)env;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  int out_fds[2];
  int err_fds[2];
  pid_t pid;
  const char *c = sz_string_cstr(st->cmd);

  if (pipe(out_fds) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: pipe failed");
    return r;
  }
  if (pipe(err_fds) != 0) {
    close(out_fds[0]);
    close(out_fds[1]);
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: pipe failed");
    return r;
  }
  pid = fork();
  if (pid < 0) {
    close(out_fds[0]);
    close(out_fds[1]);
    close(err_fds[0]);
    close(err_fds[1]);
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: fork failed");
    return r;
  }
  if (pid == 0) {
    close(out_fds[0]);
    close(err_fds[0]);
    if (dup2(out_fds[1], STDOUT_FILENO) < 0)
      _exit(127);
    if (dup2(err_fds[1], STDERR_FILENO) < 0)
      _exit(127);
    if (out_fds[1] != STDOUT_FILENO)
      close(out_fds[1]);
    if (err_fds[1] != STDERR_FILENO && err_fds[1] != STDOUT_FILENO)
      close(err_fds[1]);
    exec_close_extra_fds();
    execl("/bin/sh", "sh", "-c", c, (char *)NULL);
    _exit(127);
  }
  close(out_fds[1]);
  close(err_fds[1]);
  exec_set_cloexec_nb(out_fds[0]);
  exec_set_cloexec_nb(err_fds[0]);
  st->out_fd = out_fds[0];
  st->err_fd = err_fds[0];
  st->pid = pid;
  r->is_err = 0;
  return r;
}

static void *sys_exec_pack(ExecSt *st, int code) {
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  SzString *out = sz_string_from_bytes(st->out_buf ? st->out_buf : "", st->out_len);
  SzString *err = sz_string_from_bytes(st->err_buf ? st->err_buf : "", st->err_len);
  void *boxed = sz_box_i64((int64_t)code);
  SzPair *io = sz_pair_new(out, err);
  SzPair *tup = sz_pair_new(boxed, io);
  sz_release(out);
  sz_release(err);
  sz_release(boxed);
  sz_release(io);
  r->is_err = 0;
  r->as.ok = tup;
  return r;
}

static int exec_exit_code(int status) {
  int code = status;
#ifdef WIFEXITED
  if (WIFEXITED(status))
    return WEXITSTATUS(status);
#endif
#ifdef WIFSIGNALED
  if (WIFSIGNALED(status))
    return 128 + WTERMSIG(status);
#endif
  return code;
}

static SzIo *exec_wait_ready(ExecSt *st);

static SzIo *exec_after_poll(void *value, void *env) {
  ExecSt *st = (ExecSt *)env;
  int status = 0;
  pid_t w = 0;
  (void)value;
  if (!exec_drain_fd(&st->out_fd, &st->out_buf, &st->out_len, &st->out_cap,
                     &st->overflow) ||
      !exec_drain_fd(&st->err_fd, &st->err_buf, &st->err_len, &st->err_cap,
                     &st->overflow)) {
    if (st->overflow)
      return sz_io_fail_cstr("Sys.exec: output exceeds 1 MiB");
    return sz_io_fail_cstr("Sys.exec: read failed");
  }
  if (st->pid > 0) {
    do {
      w = waitpid(st->pid, &status, WNOHANG);
    } while (w < 0 && errno == EINTR);
    if (w > 0) {
      st->status = status;
      st->pid = 0;
    } else if (w < 0)
      return sz_io_fail_cstr("Sys.exec: wait failed");
  }
  if (st->out_fd >= 0 || st->err_fd >= 0)
    return exec_wait_ready(st);
  if (st->pid > 0) {
    if (w == 0)
      return fm_drop(sz_io_sleep_ms(1), exec_after_poll, st);
    if (w < 0)
      return sz_io_fail_cstr("Sys.exec: wait failed");
  }
  return unwrap_sys(sys_exec_pack(st, exec_exit_code(st->status)), NULL);
}

static SzIo *exec_wait_ready(ExecSt *st) {
  SzIo *ready;
  if (st->out_fd >= 0 && st->err_fd >= 0)
    ready = race_drop(sz_io_poll_readable(st->out_fd),
                      sz_io_poll_readable(st->err_fd));
  else if (st->out_fd >= 0)
    ready = sz_io_poll_readable(st->out_fd);
  else if (st->err_fd >= 0)
    ready = sz_io_poll_readable(st->err_fd);
  else
    ready = sz_io_pure(NULL);
  return fm_drop(ready, exec_after_poll, st);
}

static SzIo *exec_after_start(void *value, void *env) {
  ExecSt *st = (ExecSt *)env;
  SysResult *r = (SysResult *)value;
  if (!r || r->is_err)
    return unwrap_sys(value, NULL);
  sz_release(r);
  return exec_wait_ready(st);
}

static SzIo *exec_after_kick(void *ignored, void *env) {
  SzPair *p = (SzPair *)env;
  ExecSt *st = (ExecSt *)sz_rc_alloc(sizeof(ExecSt), SZ_RC_BOX);
  SzIo *io;
  (void)ignored;
  memset(st, 0, sizeof(ExecSt));
  sz_retain(p->left);
  st->cmd = (SzString *)p->left;
  st->out_fd = -1;
  st->err_fd = -1;
  io = fm_drop(sz_io_delay(sys_exec_start, st), exec_after_start, st);
  {
    SzIo *fin = sz_io_delay(exec_cleanup, st);
    SzIo *ens = sz_io_ensure(io, fin);
    sz_release(io);
    sz_release(fin);
    sz_release(st);
    return ens;
  }
}

static SzIo *sys_after_exec_dispatch(void *value, void *env) {
  SzPair *p = (SzPair *)env;
  if ((intptr_t)value)
    return sz_io_fail_cstr("Sys.exec: rejected under TestRuntime");
  return fm_drop(sz_io_pure(NULL), exec_after_kick, p);
}

SzIo *sz_sys_exec(SzString *cmd) {
  SzPair *p;
  if (!cmd)
    sz_panic("sz_sys_exec(null)");
  p = sz_pair_new(cmd, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL), sys_after_exec_dispatch,
                       p);
    sz_release(p);
    return io;
  }
}

#define CHILD_MAX 16
typedef struct {
  pid_t pid;
  int in_fd;
  int out_fd;
  int reaped;   /* waitpid collected the child; pid may be reused */
  int overflow; /* output exceeded the 1 MiB cap; bytes were dropped */
  char *buf;
  size_t len;
  size_t cap;
} ChildSlot;

static ChildSlot g_children[CHILD_MAX];

static void child_ignore_sigpipe(void) {
  static int done = 0;
  if (done)
    return;
  done = 1;
  signal(SIGPIPE, SIG_IGN);
}

static ChildSlot *child_find(pid_t pid) {
  int i;
  if (pid <= 0)
    return NULL;
  for (i = 0; i < CHILD_MAX; i++) {
    if (g_children[i].pid == pid)
      return &g_children[i];
  }
  return NULL;
}

static ChildSlot *child_reserve(void) {
  int i;
  for (i = 0; i < CHILD_MAX; i++) {
    if (g_children[i].pid == 0)
      return &g_children[i];
  }
  return NULL;
}

static void child_clear(ChildSlot *c) {
  if (!c || c->pid == 0)
    return;
  exec_close_fd(&c->in_fd);
  exec_close_fd(&c->out_fd);
  sz_free(c->buf);
  c->buf = NULL;
  c->len = 0;
  c->cap = 0;
  c->reaped = 0;
  c->overflow = 0;
  c->pid = 0;
}

static int child_drain(ChildSlot *c) {
  if (!c)
    return 1;
  return exec_drain_fd(&c->out_fd, &c->buf, &c->len, &c->cap, &c->overflow);
}

static void child_gc(void) {
  int i;
  for (i = 0; i < CHILD_MAX; i++) {
    ChildSlot *c = &g_children[i];
    int status = 0;
    pid_t w;
    if (c->pid <= 0)
      continue;
    do {
      w = waitpid(c->pid, &status, WNOHANG);
    } while (w < 0 && errno == EINTR);
    if (w == 0)
      continue;
    c->reaped = 1;
    (void)child_drain(c);
    exec_close_fd(&c->in_fd);
    exec_close_fd(&c->out_fd);
    if (c->len == 0)
      child_clear(c);
  }
}

static void child_drop_pid(pid_t pid) {
  ChildSlot *c = child_find(pid);
  if (c)
    child_clear(c);
}

static void *sys_spawn_result(void *env) {
  SzPair *p = (SzPair *)env;
  SzString *cmd = p ? (SzString *)p->left : NULL;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  ChildSlot *slot;
  int in_fds[2];
  int out_fds[2];
  pid_t pid;
  const char *c = cmd ? sz_string_cstr(cmd) : "";

  child_ignore_sigpipe();
  child_gc();
  slot = child_reserve();
  if (!slot) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.spawn: too many children");
    return r;
  }
  if (pipe(in_fds) != 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.spawn: pipe failed");
    return r;
  }
  if (pipe(out_fds) != 0) {
    close(in_fds[0]);
    close(in_fds[1]);
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.spawn: pipe failed");
    return r;
  }
  pid = fork();
  if (pid < 0) {
    close(in_fds[0]);
    close(in_fds[1]);
    close(out_fds[0]);
    close(out_fds[1]);
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.spawn: fork failed");
    return r;
  }
  if (pid == 0) {
    if (dup2(in_fds[0], STDIN_FILENO) < 0)
      _exit(127);
    if (dup2(out_fds[1], STDOUT_FILENO) < 0)
      _exit(127);
    if (in_fds[0] != STDIN_FILENO)
      close(in_fds[0]);
    if (in_fds[1] != STDIN_FILENO && in_fds[1] != STDOUT_FILENO)
      close(in_fds[1]);
    if (out_fds[0] != STDIN_FILENO && out_fds[0] != STDOUT_FILENO)
      close(out_fds[0]);
    if (out_fds[1] != STDOUT_FILENO && out_fds[1] != STDIN_FILENO)
      close(out_fds[1]);
    exec_close_extra_fds();
    execl("/bin/sh", "sh", "-c", c, (char *)NULL);
    _exit(127);
  }
  close(in_fds[0]);
  close(out_fds[1]);
  exec_set_cloexec_nb(in_fds[1]);
  exec_set_cloexec_nb(out_fds[0]);
  slot->in_fd = in_fds[1];
  slot->out_fd = out_fds[0];
  slot->buf = NULL;
  slot->len = 0;
  slot->cap = 0;
  slot->reaped = 0;
  slot->overflow = 0;
  slot->pid = pid;
  r->is_err = 0;
  r->as.ok = sz_box_i64((int64_t)pid);
  return r;
}

static SzIo *sys_after_spawn_dispatch(void *value, void *env) {
  SzPair *p = (SzPair *)env;
  if ((intptr_t)value)
    return sz_io_fail_cstr("Sys.spawn: rejected under TestRuntime");
  return fm_drop(sz_io_delay(sys_spawn_result, p), unwrap_sys, NULL);
}

SzIo *sz_sys_spawn(SzString *cmd) {
  SzPair *p;
  if (!cmd)
    sz_panic("sz_sys_spawn(null)");
  p = sz_pair_new(cmd, NULL);
  {
    SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL), sys_after_spawn_dispatch,
                       p);
    sz_release(p);
    return io;
  }
}

static void *sys_alive_result(void *env) {
  int64_t pid = sz_unbox_i64(env);
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  ChildSlot *c;
  r->is_err = 0;
  child_gc();
  if (sz_testrt_sys_is_fake()) {
    r->as.ok = sz_box_i64(sz_testrt_proc_alive(pid));
    return r;
  }
  c = child_find((pid_t)pid);
  if (c) {
    /* Our child. child_gc reaped it when dead. */
    r->as.ok = sz_box_i64(c->reaped ? 0 : 1);
    return r;
  }
  /* Not a spawned child. Probe with kill(pid, 0). A waitpid here would reap
   * a child owned by other code and report a live non-child as dead. */
  if (pid > 0 && (kill((pid_t)pid, 0) == 0 || errno != ESRCH))
    r->as.ok = sz_box_i64(1);
  else
    r->as.ok = sz_box_i64(0);
  return r;
}

SzIo *sz_sys_alive(int64_t pid) {
  void *p = sz_box_i64(pid);
  SzIo *io = fm_drop(sz_io_delay(sys_alive_result, p), unwrap_sys, NULL);
  sz_release(p);
  return io;
}

static void *sys_kill_result(void *env) {
  int64_t pid = sz_unbox_i64(env);
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  r->is_err = 0;
  r->as.ok = NULL;
  child_gc();
  if (sz_testrt_sys_is_fake()) {
    sz_testrt_proc_kill(pid);
    return r;
  }
  {
    ChildSlot *c = child_find((pid_t)pid);
    int status = 0;
    pid_t w = 0;
    /* Skip SIGTERM for a child that child_gc already reaped. Its pid may
     * name a new process now. */
    if (!(c && c->reaped) && pid > 0 && kill((pid_t)pid, SIGTERM) != 0 &&
        errno != ESRCH) {
      r->is_err = 1;
      r->as.err = sz_error_new(3, "Sys.kill: kill failed");
      return r;
    }
    if (c)
      exec_close_fd(&c->in_fd);
    if (pid > 0) {
      do {
        w = waitpid((pid_t)pid, &status, WNOHANG);
      } while (w < 0 && errno == EINTR);
    }
    if (w > 0 || (w < 0 && errno == ECHILD) || pid <= 0)
      child_drop_pid((pid_t)pid);
  }
  return r;
}

SzIo *sz_sys_kill(int64_t pid) {
  void *p = sz_box_i64(pid);
  SzIo *io = fm_drop(sz_io_delay(sys_kill_result, p), unwrap_sys, NULL);
  sz_release(p);
  return io;
}

typedef struct {
  int64_t pid;
  SzString *s;
  size_t off;
} ChildWriteSt;

typedef struct {
  int64_t pid;
  size_t want;
} ChildReadSt;

static void child_write_drop(ChildWriteSt *st) {
  if (!st)
    return;
  sz_release(st->s);
  st->s = NULL;
}

static SzIo *child_write_keep(void *value, void *env) {
  ChildWriteSt *st = (ChildWriteSt *)env;
  (void)value;
  child_write_drop(st);
  return pure_drop(NULL);
}

static SzIo *child_write_on_err(SzError *err, void *env) {
  child_write_drop((ChildWriteSt *)env);
  return fail_drop(err);
}

static SzIo *child_write_pump(void *value, void *env);

static SzIo *child_write_after_try(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  ChildWriteSt *st = (ChildWriteSt *)env;
  if (r && r->retry) {
    ChildSlot *c;
    sz_release(r);
    child_gc();
    c = child_find((pid_t)st->pid);
    if (!c || c->in_fd < 0)
      return sz_io_fail_cstr("Sys.childWrite: stdin closed");
    return fm_drop(sz_io_poll_writable(c->in_fd), child_write_pump, st);
  }
  return unwrap_sys(value, NULL);
}

static void *child_write_try(void *env) {
  ChildWriteSt *st = (ChildWriteSt *)env;
  ChildSlot *c;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  const char *p;
  size_t n;
  child_gc();
  c = child_find((pid_t)st->pid);
  if (!c) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.childWrite: unknown pid");
    return r;
  }
  if (c->in_fd < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.childWrite: stdin closed");
    return r;
  }
  p = st->s ? sz_string_cstr(st->s) : "";
  n = st->s ? (size_t)sz_string_len(st->s) : 0;
  while (st->off < n) {
    ssize_t w = write(c->in_fd, p + st->off, n - st->off);
    if (w > 0) {
      st->off += (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR)
      continue;
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      r->retry = 1;
      return r;
    }
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.childWrite: write failed");
    return r;
  }
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

static SzIo *child_write_pump(void *value, void *env) {
  (void)value;
  return fm_drop(sz_io_delay(child_write_try, env), child_write_after_try, env);
}

static SzIo *child_write_after_dispatch(void *value, void *env) {
  ChildWriteSt *st = (ChildWriteSt *)env;
  SzIo *io;
  if ((intptr_t)value) {
    child_write_drop(st);
    return sz_io_fail_cstr("Sys.childWrite: rejected under TestRuntime");
  }
  io = child_write_pump(NULL, st);
  {
    SzIo *handled = sz_io_handle_error_with(io, child_write_on_err, st);
    SzIo *done = sz_io_flatmap(handled, child_write_keep, st);
    sz_release(io);
    sz_release(handled);
    return done;
  }
}

SzIo *sz_sys_child_write(int64_t pid, SzString *s) {
  ChildWriteSt *st;
  if (!s)
    sz_panic("sz_sys_child_write(null)");
  st = (ChildWriteSt *)sz_rc_alloc(sizeof(ChildWriteSt), SZ_RC_BOX);
  memset(st, 0, sizeof(ChildWriteSt));
  st->pid = pid;
  sz_retain(s);
  st->s = s;
  st->off = 0;
  {
    SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL),
                       child_write_after_dispatch, st);
    sz_release(st);
    return io;
  }
}

static int child_take(ChildSlot *c, SysResult *r, size_t want) {
  size_t n;
  if (!c || c->len < want)
    return 0;
  n = want;
  r->is_err = 0;
  r->as.ok = sz_string_from_bytes(c->buf ? c->buf : "", n);
  if (n < c->len)
    memmove(c->buf, c->buf + n, c->len - n);
  c->len -= n;
  return 1;
}

static SzIo *child_read_pump(void *value, void *env);

static SzIo *child_read_after_try(void *value, void *env) {
  SysResult *r = (SysResult *)value;
  ChildReadSt *st = (ChildReadSt *)env;
  if (r && r->retry) {
    ChildSlot *c;
    sz_release(r);
    child_gc();
    c = child_find((pid_t)st->pid);
    if (!c)
      return sz_io_fail_cstr("Sys.childRead: unknown pid");
    if (c->out_fd < 0)
      return child_read_pump(NULL, st);
    return fm_drop(sz_io_poll_readable(c->out_fd), child_read_pump, st);
  }
  return unwrap_sys(value, NULL);
}

static void *child_read_try(void *env) {
  ChildReadSt *st = (ChildReadSt *)env;
  ChildSlot *c;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  child_gc();
  c = child_find((pid_t)st->pid);
  if (!c) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.childRead: unknown pid");
    return r;
  }
  if (!child_drain(c) || c->overflow) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, c->overflow
                                    ? "Sys.childRead: output exceeds 1 MiB"
                                    : "Sys.childRead: read failed");
    return r;
  }
  if (child_take(c, r, st->want))
    return r;
  if (c->out_fd < 0) {
    size_t n = c->len < st->want ? c->len : st->want;
    r->is_err = 0;
    r->as.ok = sz_string_from_bytes(c->buf ? c->buf : "", n);
    if (n < c->len)
      memmove(c->buf, c->buf + n, c->len - n);
    c->len -= n;
    if (c->len == 0)
      child_clear(c);
    return r;
  }
  r->retry = 1;
  return r;
}

static SzIo *child_read_pump(void *value, void *env) {
  (void)value;
  return fm_drop(sz_io_delay(child_read_try, env), child_read_after_try, env);
}

static SzIo *child_read_after_dispatch(void *value, void *env) {
  if ((intptr_t)value)
    return sz_io_fail_cstr("Sys.childRead: rejected under TestRuntime");
  return child_read_pump(NULL, env);
}

SzIo *sz_sys_child_read(int64_t pid, int64_t n) {
  ChildReadSt *st;
  size_t want = n < 0 ? 0 : (size_t)n;
  if (want > EXEC_CAP)
    want = EXEC_CAP;
  st = (ChildReadSt *)sz_rc_alloc(sizeof(ChildReadSt), SZ_RC_BOX);
  memset(st, 0, sizeof(ChildReadSt));
  st->pid = pid;
  st->want = want;
  {
    SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL),
                       child_read_after_dispatch, st);
    sz_release(st);
    return io;
  }
}

static void *child_close_try(void *env) {
  int64_t pid = sz_unbox_i64(env);
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  ChildSlot *c;
  child_gc();
  c = child_find((pid_t)pid);
  /* A child that already exited is reclaimed by child_gc. Its stdin is
   * closed, so close succeeds. Sys.kill tolerates ESRCH the same way. */
  if (c)
    exec_close_fd(&c->in_fd);
  r->is_err = 0;
  r->as.ok = NULL;
  return r;
}

static SzIo *child_close_after_dispatch(void *value, void *env) {
  if ((intptr_t)value)
    return sz_io_fail_cstr("Sys.childClose: rejected under TestRuntime");
  return fm_drop(sz_io_delay(child_close_try, env), unwrap_sys, NULL);
}

SzIo *sz_sys_child_close(int64_t pid) {
  void *p = sz_box_i64(pid);
  SzIo *io = fm_drop(sz_io_delay(sys_fake_dispatch, NULL),
                     child_close_after_dispatch, p);
  sz_release(p);
  return io;
}

static void *sys_getenv_result(void *env) {
  SzPair *p = (SzPair *)env;
  SzString *key = p ? (SzString *)p->left : NULL;
  SysResult *r = (SysResult *)rc_box_zero(sizeof(SysResult));
  const char *v;
  sz_timeline_log_cstr("Sys.getenv", key ? sz_string_cstr(key) : "");
  if (sz_testrt_sys_is_fake())
    v = sz_testrt_env_get(key ? sz_string_cstr(key) : "");
  else
    v = getenv(key ? sz_string_cstr(key) : "");
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(v ? v : "");
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
