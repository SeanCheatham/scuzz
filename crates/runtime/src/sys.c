#define _POSIX_C_SOURCE 200809L
#include "scalui_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* Process / args kit for Stage-1 CLI + clang link (self-host). */

static char **g_argv = NULL;
static int g_argc = 0;

void su_sys_set_args(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
}

static void *sys_args_thunk(void *env) {
  (void)env;
  SuList *acc = su_list_nil();
  /* Skip argv[0] (program name); expose user args only. */
  for (int i = g_argc - 1; i >= 1; i--)
    acc = su_list_cons(su_string_from_cstr(g_argv[i] ? g_argv[i] : ""), acc);
  return acc;
}

SuIo *su_sys_args(void) {
  return su_io_delay(sys_args_thunk, NULL);
}

typedef struct {
  int is_err;
  union {
    SuError *err;
    void *ok;
  } as;
} SysResult;

static SuIo *unwrap_sys(void *value, void *env) {
  (void)env;
  SysResult *r = (SysResult *)value;
  if (!r)
    return su_io_fail_cstr("Sys: null result");
  if (r->is_err)
    return su_io_fail(r->as.err);
  return su_io_pure(r->as.ok);
}

static void *sys_exec_result(void *env) {
  SuString *cmd = (SuString *)env;
  SysResult *r = (SysResult *)su_alloc(sizeof(SysResult));
  const char *c = su_string_cstr(cmd);
  int status = system(c);
  if (status < 0) {
    r->is_err = 1;
    r->as.err = su_error_new(3, "Sys.exec: system() failed");
    return r;
  }
  /* Normalize to exit code when possible. */
  int code = status;
#ifdef WIFEXITED
  if (WIFEXITED(status))
    code = WEXITSTATUS(status);
#endif
  r->is_err = 0;
  r->as.ok = su_box_i64((int64_t)code);
  return r;
}

SuIo *su_sys_exec(SuString *cmd) {
  if (!cmd)
    su_panic("su_sys_exec(null)");
  return su_io_flatmap(su_io_delay(sys_exec_result, cmd), unwrap_sys, NULL);
}

static void *sys_getenv_result(void *env) {
  SuString *key = (SuString *)env;
  SysResult *r = (SysResult *)su_alloc(sizeof(SysResult));
  const char *v = getenv(su_string_cstr(key));
  r->is_err = 0;
  r->as.ok = su_string_from_cstr(v ? v : "");
  return r;
}

SuIo *su_sys_getenv(SuString *key) {
  if (!key)
    su_panic("su_sys_getenv(null)");
  return su_io_flatmap(su_io_delay(sys_getenv_result, key), unwrap_sys, NULL);
}
