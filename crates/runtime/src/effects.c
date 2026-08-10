#include "scalui_rt.h"

/* Effects kit: Ref / Deferred / Queue / race / both / sleep / errors. */

/* flatMap(println, cont) invokes cont(NULL, env) — pass payload via env. */
static SuIo *println_env(void *value, void *env) {
  (void)value;
  SuString *s = (SuString *)env;
  return su_io_println(s ? s : su_string_from_cstr("(null)"));
}

static SuIo *labeled(const char *label, SuString *value) {
  return su_io_flatmap(su_io_println_cstr(label), println_env, value);
}

static SuIo *kit_after_ref_get(void *value, void *env) {
  (void)env;
  return labeled("ref:", (SuString *)value);
}

static SuIo *kit_after_ref_set(void *value, void *env) {
  (void)value;
  SuRef *r = (SuRef *)env;
  return su_io_flatmap(su_ref_get(r), kit_after_ref_get, NULL);
}

static SuIo *kit_after_ref(void *value, void *env) {
  (void)env;
  SuRef *r = (SuRef *)value;
  return su_io_flatmap(su_ref_set_cstr(r, "ref-ok"), kit_after_ref_set, r);
}

static SuIo *kit_after_q_take(void *value, void *env) {
  (void)env;
  return labeled("queue:", (SuString *)value);
}

static SuIo *kit_after_q_offer(void *value, void *env) {
  (void)value;
  SuQueue *q = (SuQueue *)env;
  return su_io_flatmap(su_queue_take(q), kit_after_q_take, NULL);
}

static SuIo *kit_after_queue(void *value, void *env) {
  (void)env;
  SuQueue *q = (SuQueue *)value;
  return su_io_flatmap(su_queue_offer_cstr(q, "queued"), kit_after_q_offer, q);
}

static SuIo *kit_after_def_get(void *value, void *env) {
  (void)env;
  return labeled("deferred:", (SuString *)value);
}

static SuIo *kit_after_def_complete(void *value, void *env) {
  (void)value;
  SuDeferred *d = (SuDeferred *)env;
  return su_io_flatmap(su_deferred_get(d), kit_after_def_get, NULL);
}

static SuIo *kit_after_deferred(void *value, void *env) {
  (void)env;
  SuDeferred *d = (SuDeferred *)value;
  return su_io_flatmap(su_deferred_complete_cstr(d, "done"),
                       kit_after_def_complete, d);
}

static SuIo *kit_recover(SuError *err, void *env) {
  (void)env;
  const char *msg = err ? su_string_cstr(err->message) : "error";
  SuString *line = su_string_from_cstr(msg);
  if (err)
    su_error_free(err);
  return labeled("recovered:", line);
}

static SuIo *kit_after_race(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_println_cstr("race-won");
}

static SuIo *kit_after_both(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_println_cstr("both-ok");
}

static SuIo *kit_after_sleep(void *value, void *env) {
  (void)value;
  (void)env;
  SuIo *raced =
      su_io_race(su_io_sleep_ms(50), su_io_println_cstr("race-fast"));
  return su_io_flatmap(raced, kit_after_race, NULL);
}

static SuIo *kit_chain_both(void *value, void *env) {
  (void)value;
  (void)env;
  SuIo *both = su_io_both(su_io_println_cstr("both-a"),
                          su_io_println_cstr("both-b"));
  return su_io_flatmap(both, kit_after_both, NULL);
}

static SuIo *kit_after_recover(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_io_sleep_ms(1), kit_after_sleep, NULL);
}

static SuIo *kit_after_def_chain(void *value, void *env) {
  (void)value;
  (void)env;
  SuIo *failed = su_io_fail_cstr("boom");
  SuIo *handled = su_io_handle_error_with(failed, kit_recover, NULL);
  return su_io_flatmap(handled, kit_after_recover, NULL);
}

static SuIo *kit_after_queue_chain(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_deferred_empty(), kit_after_deferred, NULL);
}

static SuIo *kit_after_ref_chain(void *value, void *env) {
  (void)value;
  (void)env;
  return su_io_flatmap(su_queue_unbounded(), kit_after_queue, NULL);
}

SuIo *su_effects_run_kit(void) {
  SuIo *start = su_io_flatmap(su_ref_of_cstr("x"), kit_after_ref, NULL);
  SuIo *q = su_io_flatmap(start, kit_after_ref_chain, NULL);
  SuIo *d = su_io_flatmap(q, kit_after_queue_chain, NULL);
  SuIo *err = su_io_flatmap(d, kit_after_def_chain, NULL);
  return su_io_flatmap(err, kit_chain_both, NULL);
}
