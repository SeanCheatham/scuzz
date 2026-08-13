#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Process / args / console kit for Stage-1 CLI + clang link (self-host). */

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
    for (int i = g_argc - 1; i >= 1; i--)
      acc = sz_list_cons(sz_string_from_cstr(g_argv[i] ? g_argv[i] : ""), acc);
    return acc;
  }
}

SzIo *sz_sys_args(void) {
  return sz_io_delay(sys_args_thunk, NULL);
}

typedef struct {
  int is_err;
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
  if (r->is_err)
    return sz_io_fail(r->as.err);
  return sz_io_pure(r->as.ok);
}

static void *sys_read_line_result(void *env) {
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  char *line = NULL;
  size_t cap = 0;
  ssize_t n;
  (void)env;
  n = getline(&line, &cap, stdin);
  if (n < 0) {
    int err = errno;
    free(line);
    if (feof(stdin)) {
      clearerr(stdin);
      r->is_err = 0;
      r->as.ok = sz_string_from_cstr("");
      return r;
    }
    r->is_err = 1;
    r->as.err = sz_error_new(3, err ? strerror(err) : "Sys.readLine: read failed");
    return r;
  }
  /* Strip trailing \n and optional \r. */
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    n--;
  r->is_err = 0;
  r->as.ok = sz_string_from_bytes(line, (size_t)n);
  free(line);
  return r;
}

SzIo *sz_sys_read_line(void) {
  if (sz_testrt_sys_is_fake())
    return sz_testrt_sys_read_line();
  return sz_io_flatmap(sz_io_delay(sys_read_line_result, NULL), unwrap_sys, NULL);
}

static void *sys_exec_result(void *env) {
  SzString *cmd = (SzString *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  const char *c = sz_string_cstr(cmd);
  int status = system(c);
  if (status < 0) {
    r->is_err = 1;
    r->as.err = sz_error_new(3, "Sys.exec: system() failed");
    return r;
  }
  /* Normalize to exit code when possible. */
  int code = status;
#ifdef WIFEXITED
  if (WIFEXITED(status))
    code = WEXITSTATUS(status);
#endif
  r->is_err = 0;
  r->as.ok = sz_box_i64((int64_t)code);
  return r;
}

SzIo *sz_sys_exec(SzString *cmd) {
  if (!cmd)
    sz_panic("sz_sys_exec(null)");
  return sz_io_flatmap(sz_io_delay(sys_exec_result, cmd), unwrap_sys, NULL);
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
    return r;
  }
  if (pid == 0) {
    execl("/bin/sh", "sh", "-c", c, (char *)NULL);
    _exit(127);
  }
  r->is_err = 0;
  r->as.ok = sz_box_i64((int64_t)pid);
  return r;
}

SzIo *sz_sys_spawn(SzString *cmd) {
  if (!cmd)
    sz_panic("sz_sys_spawn(null)");
  return sz_io_flatmap(sz_io_delay(sys_spawn_result, cmd), unwrap_sys, NULL);
}

static void *sys_alive_result(void *env) {
  int64_t pid = *(int64_t *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  int status = 0;
  pid_t w;
  sz_free(env);
  w = waitpid((pid_t)pid, &status, WNOHANG);
  r->is_err = 0;
  if (w == 0)
    r->as.ok = sz_box_i64(1);
  else
    r->as.ok = sz_box_i64(0);
  return r;
}

SzIo *sz_sys_alive(int64_t pid) {
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = pid;
  return sz_io_flatmap(sz_io_delay(sys_alive_result, p), unwrap_sys, NULL);
}

static void *sys_getenv_result(void *env) {
  SzString *key = (SzString *)env;
  SysResult *r = (SysResult *)sz_alloc(sizeof(SysResult));
  const char *v = getenv(sz_string_cstr(key));
  r->is_err = 0;
  r->as.ok = sz_string_from_cstr(v ? v : "");
  return r;
}

SzIo *sz_sys_getenv(SzString *key) {
  if (!key)
    sz_panic("sz_sys_getenv(null)");
  return sz_io_flatmap(sz_io_delay(sys_getenv_result, key), unwrap_sys, NULL);
}
