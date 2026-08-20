#include "scuzz_rt.h"

SzQueue *sz_queue_make(void) {
  SzQueue *q = (SzQueue *)sz_rc_alloc(sizeof(SzQueue), SZ_RC_QUEUE);
  q->cap = 8;
  q->head = 0;
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

static size_t queue_slot(const SzQueue *q, size_t i) {
  size_t idx = q->head + i;
  if (idx >= q->cap)
    idx -= q->cap;
  return idx;
}

static void queue_grow(SzQueue *q) {
  size_t ncap;
  void **nitems;
  size_t i;
  if (q->cap > (SIZE_MAX / 2) / sizeof(void *))
    sz_panic("sz_queue_offer: cap overflow");
  ncap = q->cap * 2;
  nitems = (void **)sz_alloc(sizeof(void *) * ncap);
  for (i = 0; i < q->len; i++)
    nitems[i] = q->items[queue_slot(q, i)];
  sz_free(q->items);
  q->items = nitems;
  q->cap = ncap;
  q->head = 0;
}

static void *queue_offer_thunk(void *env) {
  SzPair *p = (SzPair *)env;
  SzQueue *q = (SzQueue *)p->left;
  void *value = p->right;
  sz_retain(value);
  if (sz_fiber_wake_queue(q, value))
    return NULL;
  if (q->len == q->cap)
    queue_grow(q);
  q->items[queue_slot(q, q->len)] = value;
  q->len++;
  return NULL;
}

SzIo *sz_queue_offer(SzQueue *q, void *value) {
  if (!q)
    sz_panic("sz_queue_offer(null)");
  {
    SzPair *p = sz_pair_new(q, value);
    SzIo *io = sz_io_delay(queue_offer_thunk, p);
    sz_release(p);
    return io;
  }
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
