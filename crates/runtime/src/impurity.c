#include "scalui_rt.h"

#include <stdio.h>

/* Kernel demo: Clock / Random / mem FS / stub Net under TestRuntime. */

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

static SuIo *after_net(void *value, void *env) {
  (void)env;
  return su_io_flatmap(labeled("net:", (SuString *)value), kit_done, NULL);
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
                  su_string_from_cstr("phase6")),
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
  su_testrt_install();
  su_testrt_net_stub("http://example.test/v1", "stub-body");
  return su_io_flatmap(su_clock_real_time(), after_real, NULL);
}
