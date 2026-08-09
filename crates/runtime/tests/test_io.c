#include "scalui_rt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int delay_calls = 0;
static void *delay_inc(void *env) {
  (void)env;
  delay_calls++;
  return (void *)(intptr_t)42;
}

static SuIo *cont_println(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_println_cstr("after-flatmap");
}

static int released = 0;
static void *acquire_token(void *env) {
  (void)env;
  return (void *)(intptr_t)7;
}
static void release_token(void *value, void *env) {
  (void)env;
  assert((intptr_t)value == 7);
  released = 1;
}
static SuIo *use_token(void *acquired, void *env) {
  (void)env;
  assert((intptr_t)acquired == 7);
  return su_io_println_cstr("resource-used");
}

int main(void) {
  /* delay */
  delay_calls = 0;
  SuIo *d = su_io_delay(delay_inc, NULL);
  SuIoResult r = su_io_unsafe_run(d);
  assert(r.ok);
  assert((intptr_t)r.value == 42);
  assert(delay_calls == 1);

  /* println + flatMap */
  SuIo *prog =
      su_io_flatmap(su_io_println_cstr("hello"), cont_println, NULL);
  r = su_io_unsafe_run(prog);
  assert(r.ok);

  /* fail */
  r = su_io_unsafe_run(su_io_fail(su_error_new(9, "boom")));
  assert(!r.ok);
  assert(r.error && strstr(su_string_cstr(r.error->message), "boom"));
  su_error_free(r.error);

  /* Resource bracket */
  released = 0;
  SuResource *res = su_resource_make(acquire_token, release_token, NULL);
  SuIo *rio = su_resource_use(res, use_token, NULL);
  r = su_io_unsafe_run(rio);
  assert(r.ok);
  assert(released == 1);
  su_resource_free(res);

  puts("runtime io tests ok");
  return 0;
}
