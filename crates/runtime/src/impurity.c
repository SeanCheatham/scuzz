#include "scuzz_rt.h"
#include "rt_util.h"

#include <stdio.h>
#include <string.h>

/* Impurity kit: Clock / Random / mem FS / stub Net / Sys console under TestRuntime.
 * Boot replaces fake net stubs, argv, and stdin, and resets fake stdout. */

static SzIo *println_env(void *value, void *env) {
  (void)value;
  return sz_io_println((SzString *)env);
}

static SzIo *labeled(const char *label, SzString *value) {
  return fm_drop(sz_io_println_cstr(label), println_env, value);
}

static SzIo *label_i64(const char *label, int64_t n) {
  char buf[64];
  SzString *s;
  SzIo *io;
  snprintf(buf, sizeof buf, "%lld", (long long)n);
  s = sz_string_from_cstr(buf);
  io = labeled(label, s);
  sz_release(s);
  return io;
}

static SzIo *kit_done(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("impurity-ok");
}

static SzIo *after_line(void *value, void *env) {
  SzIo *io;
  (void)env;
  io = labeled("line:", (SzString *)value);
  sz_release(value);
  return fm_drop(io, kit_done, NULL);
}

static SzIo *do_read_line(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_sys_read_line(), after_line, NULL);
}

static SzIo *after_args(void *value, void *env) {
  SzList *xs = (SzList *)value;
  SzString *sep;
  SzString *joined;
  SzIo *io;
  (void)env;
  sep = sz_string_from_cstr(",");
  joined = sz_list_join(xs, sep);
  sz_release(sep);
  sz_release(xs);
  io = labeled("args:", joined);
  sz_release(joined);
  return fm_drop(io, do_read_line, NULL);
}

static SzIo *do_args(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_sys_args(), after_args, NULL);
}

static SzIo *after_net(void *value, void *env) {
  SzIo *io;
  (void)env;
  io = labeled("net:", (SzString *)value);
  sz_release(value);
  return fm_drop(io, do_args, NULL);
}

static SzIo *do_net(void *value, void *env) {
  SzString *url;
  SzIo *io;
  (void)value;
  (void)env;
  url = sz_string_from_cstr("http://example.test/v1");
  io = sz_net_http_get(url);
  sz_release(url);
  return fm_drop(io, after_net, NULL);
}

static SzIo *after_fs(void *value, void *env) {
  SzIo *io;
  (void)env;
  io = labeled("fs:", (SzString *)value);
  sz_release(value);
  return fm_drop(io, do_net, NULL);
}

static SzIo *do_fs_read(void *value, void *env) {
  SzString *path;
  SzIo *io;
  (void)value;
  (void)env;
  path = sz_string_from_cstr("note.txt");
  io = sz_fs_read(path);
  sz_release(path);
  return fm_drop(io, after_fs, NULL);
}

static SzIo *do_fs_write(void *value, void *env) {
  SzString *path;
  SzString *body;
  SzIo *io;
  (void)value;
  (void)env;
  path = sz_string_from_cstr("note.txt");
  body = sz_string_from_cstr("kit-note");
  io = sz_fs_write(path, body);
  sz_release(path);
  sz_release(body);
  return fm_drop(io, do_fs_read, NULL);
}

static SzIo *after_rand(void *value, void *env) {
  int64_t n = sz_unbox_i64(value);
  (void)env;
  sz_release(value);
  return fm_drop(label_i64("rand:", n), do_fs_write, NULL);
}

static SzIo *do_rand(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_random_next_int(100), after_rand, NULL);
}

static SzIo *do_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_io_sleep_ms(50), do_rand, NULL);
}

static SzIo *after_mono(void *value, void *env) {
  int64_t n = sz_unbox_i64(value);
  (void)env;
  sz_release(value);
  return fm_drop(label_i64("mono:", n), do_sleep, NULL);
}

static SzIo *do_mono(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_clock_monotonic(), after_mono, NULL);
}

static SzIo *after_real(void *value, void *env) {
  int64_t n = sz_unbox_i64(value);
  (void)env;
  sz_release(value);
  return fm_drop(label_i64("real:", n), do_mono, NULL);
}

static void *impurity_unit_thunk(void *env) {
  (void)env;
  return NULL;
}

/* Overlay stubs when TestRuntime is already active. Do not install it. */
static SzIo *impurity_boot(void *value, void *env) {
  char *argv[] = {"alpha", "beta"};
  (void)value;
  (void)env;
  if (!sz_testrt_clock_is_fake())
    return sz_io_fail_cstr("Impurity.runKit requires TestRuntime");
  sz_testrt_net_stub("http://example.test/v1", "stub-body");
  sz_testrt_sys_set_args(2, argv);
  sz_testrt_stdin_feed("hello-line\n");
  sz_testrt_stdout_reset();
  return fm_drop(sz_clock_real_time(), after_real, NULL);
}

SzIo *sz_impurity_run_kit(void) {
  return fm_drop(sz_io_delay(impurity_unit_thunk, NULL), impurity_boot, NULL);
}
