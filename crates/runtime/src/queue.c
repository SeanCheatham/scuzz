#include "scuzz_rt.h"

SzQueue *sz_queue_make(void) {
  SzQueue *q = (SzQueue *)sz_rc_alloc(sizeof(SzQueue), SZ_RC_QUEUE);
  q->cap = 8;
  q->len = 0;
  q->waiters = NULL;
  q->items = (void **)sz_alloc(sizeof(void *) * q->cap);
  return q;
}

void sz_queue_free(SzQueue *q) { sz_release(q); }

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
    sz_release(e->q);
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
  sz_release(e->q);
  sz_free(e);
  return NULL;
}

SzIo *sz_queue_offer(SzQueue *q, void *value) {
  if (!q)
    sz_panic("sz_queue_offer(null)");
  QOfferEnv *e = (QOfferEnv *)sz_alloc(sizeof(QOfferEnv));
  e->q = q;
  sz_retain(q);
  sz_retain(value);
  e->value = value;
  return sz_io_delay(queue_offer_thunk, e);
}

static SzIo *queue_offer_drop(SzQueue *q, void *value) {
  SzIo *io = sz_queue_offer(q, value);
  sz_release(value);
  return io;
}

SzIo *sz_queue_offer_cstr(SzQueue *q, const char *value) {
  return queue_offer_drop(q, sz_string_from_cstr(value ? value : ""));
}

SzIo *sz_queue_take(SzQueue *q) {
  if (!q)
    sz_panic("sz_queue_take(null)");
  /* Runtime transfers the offer retain. Compiler drops the owned take payload. */
  return sz_io_queue_take(q);
}

size_t sz_queue_size(const SzQueue *q) { return q ? q->len : 0; }
