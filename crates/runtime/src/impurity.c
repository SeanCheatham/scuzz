#include "scalui_rt.h"

#include <stdio.h>
#include <string.h>

/* Impurity kit: Clock / Random / mem FS / stub Net / Sys console under TestRuntime. */

static SuIo *println_env(void *value, void *env) {
  (void)value;
  return su_io_println((SuString *)env);
}

static SuIo *labeled(const char *label, SuString *value) {
  return su_io_flatmap(su_io_println_cstr(label), println_env, value);
}

static SuIo *label_i64(const char *label, int64_t n) {
  char buf[64];
  snprintf(buf, sizeof buf, "%lld", (long long)n);
  return labeled(label, su_string_from_cstr(buf));
}

static SuIo *kit_done(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_println_cstr("impurity-ok");
}

static SuIo *after_line(void *value, void *env) {
  (void)env;
  return su_io_flatmap(labeled("line:", (SuString *)value), kit_done, NULL);
}

static SuIo *do_read_line(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_sys_read_line(), after_line, NULL);
}

static SuIo *after_args(void *value, void *env) {
  (void)env;
  SuList *xs = (SuList *)value;
  SuString *joined = su_list_join(xs, ",");
  return su_io_flatmap(labeled("args:", joined), do_read_line, NULL);
}

static SuIo *do_args(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_sys_args(), after_args, NULL);
}

static SuIo *after_net(void *value, void *env) {
  (void)env;
  return su_io_flatmap(labeled("net:", (SuString *)value), do_args, NULL);
}

static SuIo *do_net(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(
      su_net_http_get(su_string_from_cstr("http://example.test/v1")), after_net,
      NULL);
}

static SuIo *after_fs(void *value, void *env) {
  (void)env;
  return su_io_flatmap(labeled("fs:", (SuString *)value), do_net, NULL);
}

static SuIo *do_fs_read(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_fs_read(su_string_from_cstr("note.txt")), after_fs,
                       NULL);
}

static SuIo *do_fs_write(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(
      su_fs_write(su_string_from_cstr("note.txt"),
                  su_string_from_cstr("kit-note")),
      do_fs_read, NULL);
}

static SuIo *after_rand(void *value, void *env) {
  (void)env;
  return su_io_flatmap(label_i64("rand:", su_unbox_i64(value)), do_fs_write,
                       NULL);
}

static SuIo *do_rand(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_random_next_int(100), after_rand, NULL);
}

static SuIo *after_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  return do_rand(NULL, NULL);
}

static SuIo *do_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_io_sleep_ms(50), after_sleep, NULL);
}

static SuIo *after_mono(void *value, void *env) {
  (void)env;
  return su_io_flatmap(label_i64("mono:", su_unbox_i64(value)), do_sleep, NULL);
}

static SuIo *do_mono(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_clock_monotonic(), after_mono, NULL);
}

static SuIo *after_real(void *value, void *env) {
  (void)env;
  return su_io_flatmap(label_i64("real:", su_unbox_i64(value)), do_mono, NULL);
}

SuIo *su_impurity_run_kit(void) {
  char *argv[] = {"alpha", "beta"};
  su_testrt_install();
  su_testrt_net_stub("http://example.test/v1", "stub-body");
  su_testrt_sys_set_args(2, argv);
  su_testrt_stdin_feed("hello-line\n");
  su_testrt_stdout_reset();
  return su_io_flatmap(su_clock_real_time(), after_real, NULL);
}
