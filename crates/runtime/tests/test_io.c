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

static SuIo *use_token_fail(void *acquired, void *env) {
  (void)env;
  assert((intptr_t)acquired == 7);
  return su_io_fail_cstr("use-failed");
}

static SuIo *recover_boom(SuError *err, void *env) {
  (void)env;
  assert(err && strstr(su_string_cstr(err->message), "boom"));
  su_error_free(err);
  return su_io_println_cstr("recovered");
}

static SuIo *after_ref_get(void *value, void *env) {
  (void)env;
  SuString *s = (SuString *)value;
  assert(s && strcmp(su_string_cstr(s), "b") == 0);
  return su_io_pure(NULL);
}

static SuIo *after_ref_set(void *value, void *env) {
  (void)value;
  SuRef *r = (SuRef *)env;
  return su_io_flatmap(su_ref_get(r), after_ref_get, NULL);
}

static SuIo *after_ref(void *value, void *env) {
  (void)env;
  SuRef *r = (SuRef *)value;
  return su_io_flatmap(su_ref_set_cstr(r, "b"), after_ref_set, r);
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

  /* handleErrorWith */
  r = su_io_unsafe_run(su_io_handle_error_with(su_io_fail_cstr("boom"),
                                               recover_boom, NULL));
  assert(r.ok);

  /* attempt */
  r = su_io_unsafe_run(su_io_attempt(su_io_fail_cstr("nope")));
  assert(r.ok);
  {
    SuEither *e = (SuEither *)r.value;
    assert(e && !e->is_right);
    assert(e->as.left && strstr(su_string_cstr(e->as.left->message), "nope"));
    su_either_free(e);
  }
  r = su_io_unsafe_run(su_io_attempt(su_io_pure((void *)(intptr_t)3)));
  assert(r.ok);
  {
    SuEither *e = (SuEither *)r.value;
    assert(e && e->is_right && (intptr_t)e->as.right == 3);
    su_either_free(e);
  }

  /* Resource bracket */
  released = 0;
  SuResource *res = su_resource_make(acquire_token, release_token, NULL);
  SuIo *rio = su_resource_use(res, use_token, NULL);
  r = su_io_unsafe_run(rio);
  assert(r.ok);
  assert(released == 1);
  su_resource_free(res);

  /* Resource releases on use failure */
  released = 0;
  res = su_resource_make(acquire_token, release_token, NULL);
  r = su_io_unsafe_run(su_resource_use(res, use_token_fail, NULL));
  assert(!r.ok);
  assert(released == 1);
  su_error_free(r.error);
  su_resource_free(res);

  /* Ref */
  r = su_io_unsafe_run(su_io_flatmap(su_ref_of_cstr("a"), after_ref, NULL));
  assert(r.ok);

  /* Deferred */
  {
    SuDeferred *def = su_deferred_make();
    r = su_io_unsafe_run(su_deferred_complete_cstr(def, "ok"));
    assert(r.ok);
    r = su_io_unsafe_run(su_deferred_get(def));
    assert(r.ok);
    assert(strcmp(su_string_cstr((SuString *)r.value), "ok") == 0);
    su_deferred_free(def);
  }

  /* Queue */
  {
    SuQueue *q = su_queue_make();
    r = su_io_unsafe_run(su_queue_offer_cstr(q, "x"));
    assert(r.ok);
    assert(su_queue_size(q) == 1);
    r = su_io_unsafe_run(su_queue_take(q));
    assert(r.ok);
    assert(strcmp(su_string_cstr((SuString *)r.value), "x") == 0);
    su_queue_free(q);
  }

  /* sleep */
  r = su_io_unsafe_run(su_io_sleep_ms(1));
  assert(r.ok);

  /* race prefers non-sleep winner */
  r = su_io_unsafe_run(
      su_io_race(su_io_sleep_ms(20), su_io_pure((void *)(intptr_t)99)));
  assert(r.ok);
  assert((intptr_t)r.value == 99);

  /* both */
  r = su_io_unsafe_run(
      su_io_both(su_io_pure((void *)(intptr_t)1), su_io_pure((void *)(intptr_t)2)));
  assert(r.ok);
  {
    SuPair *p = (SuPair *)r.value;
    assert(p && (intptr_t)p->left == 1 && (intptr_t)p->right == 2);
    su_pair_free(p);
  }

  /* kit demo */
  r = su_io_unsafe_run(su_effects_run_kit());
  assert(r.ok);

  /* string ops */
  {
    SuString *a = su_string_from_cstr("foo");
    SuString *b = su_string_from_cstr("bar");
    SuString *c = su_string_concat(a, b);
    assert(strcmp(su_string_cstr(c), "foobar") == 0);
    assert(su_string_len(c) == 6);
    assert(su_string_eq(c, su_string_from_cstr("foobar")));
    assert(su_string_char_at(c, 0) == 'f');
    SuString *sl = su_string_slice(c, 3, 6);
    assert(strcmp(su_string_cstr(sl), "bar") == 0);
    assert(su_string_index_of(c, b) == 3);
    assert(strcmp(su_string_cstr(su_string_from_int(42)), "42") == 0);
  }

  /* list */
  {
    SuList *xs = su_list_cons(su_string_from_cstr("a"),
                              su_list_cons(su_string_from_cstr("b"), su_list_nil()));
    assert(su_list_len(xs) == 2);
    assert(strcmp(su_string_cstr((SuString *)su_list_head(xs)), "a") == 0);
    assert(strcmp(su_string_cstr((SuString *)su_list_at(xs, 1)), "b") == 0);
    SuString *j = su_list_join(xs, ",");
    assert(strcmp(su_string_cstr(j), "a,b") == 0);
  }

  /* box */
  assert(su_unbox_i64(su_box_i64(7)) == 7);

  /* Fs roundtrip */
  {
    const char *path = "build/test_fs_roundtrip.txt";
    r = su_io_unsafe_run(su_fs_mkdirs(su_string_from_cstr("build")));
    assert(r.ok);
    r = su_io_unsafe_run(
        su_fs_write(su_string_from_cstr(path), su_string_from_cstr("phase4")));
    assert(r.ok);
    r = su_io_unsafe_run(su_fs_read(su_string_from_cstr(path)));
    assert(r.ok);
    assert(strcmp(su_string_cstr((SuString *)r.value), "phase4") == 0);
    r = su_io_unsafe_run(su_fs_list(su_string_from_cstr("build")));
    assert(r.ok);
    assert(!su_list_is_empty((SuList *)r.value));
  }

  puts("runtime io tests ok");
  return 0;
}
