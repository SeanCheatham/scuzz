#include "scuzz_rt.h"

SzQueue *sz_queue_make(void) {
  SzQueue *q = (SzQueue *)sz_alloc_zero(sizeof(SzQueue));
  q->cap = 8;
  q->items = (void **)sz_alloc(sizeof(void *) * q->cap);
  return q;
}

void sz_queue_free(SzQueue *q) {
  if (!q)
    return;
  sz_free(q->items);
  sz_free(q);
}

static void *queue_unbounded_thunk(void *env) {
  (void)env;
  return sz_queue_make();
}

SzIo *sz_queue_unbounded(void) { return sz_io_delay(queue_unbounded_thunk, NULL); }

typedef struct QOfferEnv {
  SzQueue *q;
  void *value;
} QOfferEnv;

static void *queue_offer_thunk(void *env) {
  QOfferEnv *e = (QOfferEnv *)env;
  if (sz_fiber_wake_queue(e->q, e->value)) {
    sz_free(e);
    return NULL;
  }
  if (e->q->len == e->q->cap) {
    size_t ncap = e->q->cap * 2;
    void **nitems = (void **)sz_alloc(sizeof(void *) * ncap);
    size_t i;
    for (i = 0; i < e->q->len; i++)
      nitems[i] = e->q->items[i];
    sz_free(e->q->items);
    e->q->items = nitems;
    e->q->cap = ncap;
  }
  e->q->items[e->q->len++] = e->value;
  sz_free(e);
  return NULL;
}

SzIo *sz_queue_offer(SzQueue *q, void *value) {
  if (!q)
    sz_panic("sz_queue_offer(null)");
  QOfferEnv *e = (QOfferEnv *)sz_alloc(sizeof(QOfferEnv));
  e->q = q;
  e->value = value;
  return sz_io_delay(queue_offer_thunk, e);
}

SzIo *sz_queue_offer_cstr(SzQueue *q, const char *value) {
  return sz_queue_offer(q, sz_string_from_cstr(value ? value : ""));
}

SzIo *sz_queue_take(SzQueue *q) {
  if (!q)
    sz_panic("sz_queue_take(null)");
  return sz_io_queue_take(q);
}

size_t sz_queue_size(const SzQueue *q) { return q ? q->len : 0; }
