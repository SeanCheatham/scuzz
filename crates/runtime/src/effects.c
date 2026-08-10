#include "scuzz_rt.h"

/* Effects kit: Ref / Deferred / Queue / race / both / sleep / errors. */

/* flatMap(println, cont) invokes cont(NULL, env) — pass payload via env. */
static SzIo *println_env(void *value, void *env) {
  (void)value;
  SzString *s = (SzString *)env;
  return sz_io_println(s ? s : sz_string_from_cstr("(null)"));
}

static SzIo *labeled(const char *label, SzString *value) {
  return sz_io_flatmap(sz_io_println_cstr(label), println_env, value);
}

static SzIo *kit_after_ref_get(void *value, void *env) {
  (void)env;
  return labeled("ref:", (SzString *)value);
}

static SzIo *kit_after_ref_set(void *value, void *env) {
  (void)value;
  SzRef *r = (SzRef *)env;
  return sz_io_flatmap(sz_ref_get(r), kit_after_ref_get, NULL);
}

static SzIo *kit_after_ref(void *value, void *env) {
  (void)env;
  SzRef *r = (SzRef *)value;
  return sz_io_flatmap(sz_ref_set_cstr(r, "ref-ok"), kit_after_ref_set, r);
}

static SzIo *kit_after_q_take(void *value, void *env) {
  (void)env;
  return labeled("queue:", (SzString *)value);
}

static SzIo *kit_after_q_offer(void *value, void *env) {
  (void)value;
  SzQueue *q = (SzQueue *)env;
  return sz_io_flatmap(sz_queue_take(q), kit_after_q_take, NULL);
}

static SzIo *kit_after_queue(void *value, void *env) {
  (void)env;
  SzQueue *q = (SzQueue *)value;
  return sz_io_flatmap(sz_queue_offer_cstr(q, "queued"), kit_after_q_offer, q);
}

static SzIo *kit_after_def_get(void *value, void *env) {
  (void)env;
  return labeled("deferred:", (SzString *)value);
}

static SzIo *kit_after_def_complete(void *value, void *env) {
  (void)value;
  SzDeferred *d = (SzDeferred *)env;
  return sz_io_flatmap(sz_deferred_get(d), kit_after_def_get, NULL);
}

static SzIo *kit_after_deferred(void *value, void *env) {
  (void)env;
  SzDeferred *d = (SzDeferred *)value;
  return sz_io_flatmap(sz_deferred_complete_cstr(d, "done"),
                       kit_after_def_complete, d);
}

static SzIo *kit_recover(SzError *err, void *env) {
  (void)env;
  const char *msg = err ? sz_string_cstr(err->message) : "error";
  SzString *line = sz_string_from_cstr(msg);
  if (err)
    sz_error_free(err);
  return labeled("recovered:", line);
}

static SzIo *kit_after_race(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("race-won");
}

static SzIo *kit_after_both(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_println_cstr("both-ok");
}

static SzIo *kit_after_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  SzIo *raced =
      sz_io_race(sz_io_sleep_ms(50), sz_io_println_cstr("race-fast"));
  return sz_io_flatmap(raced, kit_after_race, NULL);
}

static SzIo *kit_chain_both(void *value, void *env) {
  (void)value;
  (void)env;
  SzIo *both = sz_io_both(sz_io_println_cstr("both-a"),
                          sz_io_println_cstr("both-b"));
  return sz_io_flatmap(both, kit_after_both, NULL);
}

static SzIo *kit_after_recover(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_flatmap(sz_io_sleep_ms(1), kit_after_sleep, NULL);
}

static SzIo *kit_after_def_chain(void *value, void *env) {
  (void)value;
  (void)env;
  SzIo *failed = sz_io_fail_cstr("boom");
  SzIo *handled = sz_io_handle_error_with(failed, kit_recover, NULL);
  return sz_io_flatmap(handled, kit_after_recover, NULL);
}

static SzIo *kit_after_queue_chain(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_flatmap(sz_deferred_empty(), kit_after_deferred, NULL);
}

static SzIo *kit_after_ref_chain(void *value, void *env) {
  (void)value;
  (void)env;
  return sz_io_flatmap(sz_queue_unbounded(), kit_after_queue, NULL);
}

SzIo *sz_effects_run_kit(void) {
  SzIo *start = sz_io_flatmap(sz_ref_of_cstr("x"), kit_after_ref, NULL);
  SzIo *q = sz_io_flatmap(start, kit_after_ref_chain, NULL);
  SzIo *d = sz_io_flatmap(q, kit_after_queue_chain, NULL);
  SzIo *err = sz_io_flatmap(d, kit_after_def_chain, NULL);
  return sz_io_flatmap(err, kit_chain_both, NULL);
}
