#include "scalui_rt.h"

SuQueue *su_queue_make(void) {
  SuQueue *q = (SuQueue *)su_alloc_zero(sizeof(SuQueue));
  q->cap = 8;
  q->items = (void **)su_alloc(sizeof(void *) * q->cap);
  return q;
}

void su_queue_free(SuQueue *q) {
  if (!q)
    return;
  su_free(q->items);
  su_free(q);
}

static void *queue_unbounded_thunk(void *env) {
  (void)env;
  return su_queue_make();
}

SuIo *su_queue_unbounded(void) { return su_io_delay(queue_unbounded_thunk, NULL); }

typedef struct QOfferEnv {
  SuQueue *q;
  void *value;
} QOfferEnv;

static void *queue_offer_thunk(void *env) {
  QOfferEnv *e = (QOfferEnv *)env;
  if (e->q->len == e->q->cap) {
    size_t ncap = e->q->cap * 2;
    void **nitems = (void **)su_alloc(sizeof(void *) * ncap);
    size_t i;
    for (i = 0; i < e->q->len; i++)
      nitems[i] = e->q->items[i];
    su_free(e->q->items);
    e->q->items = nitems;
    e->q->cap = ncap;
  }
  e->q->items[e->q->len++] = e->value;
  su_free(e);
  return NULL;
}

SuIo *su_queue_offer(SuQueue *q, void *value) {
  if (!q)
    su_panic("su_queue_offer(null)");
  QOfferEnv *e = (QOfferEnv *)su_alloc(sizeof(QOfferEnv));
  e->q = q;
  e->value = value;
  return su_io_delay(queue_offer_thunk, e);
}

SuIo *su_queue_offer_cstr(SuQueue *q, const char *value) {
  return su_queue_offer(q, su_string_from_cstr(value ? value : ""));
}

static SuIo *queue_take_cont(void *value, void *env) {
  (void)value;
  SuQueue *q = (SuQueue *)env;
  if (q->len == 0)
    return su_io_fail_cstr("queue empty");
  {
    void *v = q->items[0];
    size_t i;
    for (i = 1; i < q->len; i++)
      q->items[i - 1] = q->items[i];
    q->len--;
    return su_io_pure(v);
  }
}

SuIo *su_queue_take(SuQueue *q) {
  if (!q)
    su_panic("su_queue_take(null)");
  return su_io_flatmap(su_io_pure(NULL), queue_take_cont, q);
}

size_t su_queue_size(const SuQueue *q) { return q ? q->len : 0; }
