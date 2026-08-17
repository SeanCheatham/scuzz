#include "scuzz_rt.h"

#include <stdio.h>
#include <string.h>

/* Impurity kit: Clock / Random / mem FS / stub Net / Sys console under TestRuntime. */

static SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}

static SzIo *println_env(void *value, void *env) {
  (void)value;
  return sz_io_println((SzString *)env);
}

static SzIo *labeled(const char *label, SzString *value) {
  return fm_drop(sz_io_println_cstr(label), println_env, value);
}

static SzIo *label_i64(const char *label, int64_t n) {
  char buf[64];
  snprintf(buf, sizeof buf, "%lld", (long long)n);
  return labeled(label, sz_string_from_cstr(buf));
}

static SzIo *kit_done(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("impurity-ok");
}

static SzIo *after_line(void *value, void *env) {
  (void)env;
  return fm_drop(labeled("line:", (SzString *)value), kit_done, NULL);
}

static SzIo *do_read_line(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_sys_read_line(), after_line, NULL);
}

static SzIo *after_args(void *value, void *env) {
  (void)env;
  SzList *xs = (SzList *)value;
  SzString *joined = sz_list_join(xs, ",");
  return fm_drop(labeled("args:", joined), do_read_line, NULL);
}

static SzIo *do_args(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_sys_args(), after_args, NULL);
}

static SzIo *after_net(void *value, void *env) {
  (void)env;
  return fm_drop(labeled("net:", (SzString *)value), do_args, NULL);
}

static SzIo *do_net(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(
      sz_net_http_get(sz_string_from_cstr("http://example.test/v1")), after_net,
      NULL);
}

static SzIo *after_fs(void *value, void *env) {
  (void)env;
  return fm_drop(labeled("fs:", (SzString *)value), do_net, NULL);
}

static SzIo *do_fs_read(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_fs_read(sz_string_from_cstr("note.txt")), after_fs,
                       NULL);
}

static SzIo *do_fs_write(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(
      sz_fs_write(sz_string_from_cstr("note.txt"),
                  sz_string_from_cstr("kit-note")),
      do_fs_read, NULL);
}

static SzIo *after_rand(void *value, void *env) {
  (void)env;
  return fm_drop(label_i64("rand:", sz_unbox_i64(value)), do_fs_write,
                       NULL);
}

static SzIo *do_rand(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_random_next_int(100), after_rand, NULL);
}

static SzIo *after_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  return do_rand(NULL, NULL);
}

static SzIo *do_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_io_sleep_ms(50), after_sleep, NULL);
}

static SzIo *after_mono(void *value, void *env) {
  (void)env;
  return fm_drop(label_i64("mono:", sz_unbox_i64(value)), do_sleep, NULL);
}

static SzIo *do_mono(void *value, void *env) {
  (void)value;
  (void)env;
  return fm_drop(sz_clock_monotonic(), after_mono, NULL);
}

static SzIo *after_real(void *value, void *env) {
  (void)env;
  return fm_drop(label_i64("real:", sz_unbox_i64(value)), do_mono, NULL);
}

static void *impurity_unit_thunk(void *env) {
  (void)env;
  return NULL;
}

/* Install/stub when the returned IO runs (after @main TESTRT install). */
static SzIo *impurity_boot(void *value, void *env) {
  char *argv[] = {"alpha", "beta"};
  (void)value;
  (void)env;
  sz_testrt_install();
  sz_testrt_net_stub("http://example.test/v1", "stub-body");
  sz_testrt_sys_set_args(2, argv);
  sz_testrt_stdin_feed("hello-line\n");
  sz_testrt_stdout_reset();
  return fm_drop(sz_clock_real_time(), after_real, NULL);
}

SzIo *sz_impurity_run_kit(void) {
  return fm_drop(sz_io_delay(impurity_unit_thunk, NULL), impurity_boot, NULL);
}
