#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

/* --- panic / alloc ------------------------------------------------------- */

/* Header before user pointer: [size_t nbytes][user bytes...] */
static size_t g_live_bytes = 0;
static size_t g_live_count = 0;
static size_t g_peak_bytes = 0;
static unsigned g_trace_pumps = 0;
enum { SZ_ALLOC_TRACE_EVERY = 32 };

void sz_panic(const char *msg) {
  fprintf(stderr, "scuzz panic: %s\n", msg ? msg : "(null)");
  abort();
}

void *sz_alloc(size_t size) {
  size_t *raw = (size_t *)malloc(sizeof(size_t) + size);
  if (!raw)
    sz_panic("out of memory");
  *raw = size;
  g_live_bytes += size;
  g_live_count += 1;
  if (g_live_bytes > g_peak_bytes)
    g_peak_bytes = g_live_bytes;
  return (void *)(raw + 1);
}

void *sz_alloc_zero(size_t size) {
  size_t *raw = (size_t *)calloc(1, sizeof(size_t) + size);
  if (!raw)
    sz_panic("out of memory");
  *raw = size;
  g_live_bytes += size;
  g_live_count += 1;
  if (g_live_bytes > g_peak_bytes)
    g_peak_bytes = g_live_bytes;
  return (void *)(raw + 1);
}

void sz_free(void *ptr) {
  size_t *raw;
  size_t n;
  if (!ptr)
    return;
  raw = ((size_t *)ptr) - 1;
  n = *raw;
  if (g_live_count > 0)
    g_live_count -= 1;
  if (g_live_bytes >= n)
    g_live_bytes -= n;
  else
    g_live_bytes = 0;
  free(raw);
}

void sz_alloc_stats(size_t *live_bytes, size_t *live_count) {
  if (live_bytes)
    *live_bytes = g_live_bytes;
  if (live_count)
    *live_count = g_live_count;
}

void sz_alloc_reset_stats(void) {
  g_peak_bytes = g_live_bytes;
  g_trace_pumps = 0;
}

void sz_alloc_trace_on_pump(void) {
  static int checked = 0;
  static int enabled = 0;
  const char *e;
  if (!checked) {
    e = getenv("SCUZZ_ALLOC_TRACE");
    enabled = (e && strcmp(e, "1") == 0) ? 1 : 0;
    checked = 1;
  }
  if (!enabled)
    return;
  g_trace_pumps += 1;
  if (g_trace_pumps % SZ_ALLOC_TRACE_EVERY != 0)
    return;
  fprintf(stderr, "scuzz alloc: pump=%u live_bytes=%zu live_count=%zu peak_bytes=%zu\n",
          g_trace_pumps, g_live_bytes, g_live_count, g_peak_bytes);
}

/* --- strings ------------------------------------------------------------- */

SzString *sz_string_from_bytes(const char *bytes, size_t len) {
  SzString *s = (SzString *)sz_alloc(sizeof(SzString));
  s->len = len;
  s->data = (char *)sz_alloc(len + 1);
  if (len)
    memcpy(s->data, bytes, len);
  s->data[len] = '\0';
  return s;
}

SzString *sz_string_from_cstr(const char *cstr) {
  if (!cstr)
    sz_panic("sz_string_from_cstr(null)");
  return sz_string_from_bytes(cstr, strlen(cstr));
}

const char *sz_string_cstr(const SzString *s) {
  return s && s->data ? s->data : "";
}

void sz_string_free(SzString *s) {
  if (!s)
    return;
  sz_free(s->data);
  sz_free(s);
}

SzString *sz_string_concat(const SzString *a, const SzString *b) {
  size_t al = a && a->data ? a->len : 0;
  size_t bl = b && b->data ? b->len : 0;
  char *buf = (char *)sz_alloc(al + bl + 1);
  if (al)
    memcpy(buf, a->data, al);
  if (bl)
    memcpy(buf + al, b->data, bl);
  buf[al + bl] = '\0';
  SzString *out = sz_string_from_bytes(buf, al + bl);
  sz_free(buf);
  return out;
}

int64_t sz_string_len(const SzString *s) {
  return s ? (int64_t)s->len : 0;
}

SzString *sz_string_slice(const SzString *s, int64_t start, int64_t end) {
  int64_t len = sz_string_len(s);
  if (start < 0)
    start = 0;
  if (end > len)
    end = len;
  if (start > end)
    start = end;
  size_t n = (size_t)(end - start);
  if (!s || !s->data || n == 0)
    return sz_string_from_cstr("");
  return sz_string_from_bytes(s->data + start, n);
}

int sz_string_eq(const SzString *a, const SzString *b) {
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  if (a->len != b->len)
    return 0;
  return memcmp(a->data, b->data, a->len) == 0;
}

int64_t sz_string_char_at(const SzString *s, int64_t index) {
  if (!s || index < 0 || (size_t)index >= s->len)
    return -1;
  return (unsigned char)s->data[index];
}

SzString *sz_string_from_int(int64_t n) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)n);
  return sz_string_from_cstr(buf);
}

int64_t sz_string_index_of(const SzString *s, const SzString *needle) {
  if (!s || !needle)
    return -1;
  if (needle->len == 0)
    return 0;
  if (needle->len > s->len)
    return -1;
  for (size_t i = 0; i + needle->len <= s->len; i++) {
    if (memcmp(s->data + i, needle->data, needle->len) == 0)
      return (int64_t)i;
  }
  return -1;
}

SzList *sz_string_lines(const SzString *s) {
  SzList *acc = NULL;
  size_t i = 0;
  size_t len = s && s->data ? s->len : 0;
  const char *data = s && s->data ? s->data : "";
  while (i < len) {
    size_t start = i;
    while (i < len && data[i] != '\n' && data[i] != '\r')
      i++;
    size_t end = i;
    if (i < len && data[i] == '\r')
      i++;
    if (i < len && data[i] == '\n')
      i++;
    if (end > start)
      acc = sz_list_cons(sz_string_from_bytes(data + start, end - start), acc);
  }
  return sz_list_reverse(acc);
}

void *sz_box_i64(int64_t n) {
  int64_t *p = (int64_t *)sz_alloc(sizeof(int64_t));
  *p = n;
  return p;
}

int64_t sz_unbox_i64(const void *p) {
  return p ? *(const int64_t *)p : 0;
}

/* --- errors / Either / ADT / Pair ---------------------------------------- */

SzError *sz_error_new(int32_t code, const char *msg) {
  SzError *e = (SzError *)sz_alloc(sizeof(SzError));
  e->code = code;
  e->message = sz_string_from_cstr(msg ? msg : "error");
  return e;
}

void sz_error_free(SzError *err) {
  if (!err)
    return;
  sz_string_free(err->message);
  sz_free(err);
}

SzEither *sz_either_right(void *value) {
  SzEither *e = (SzEither *)sz_alloc_zero(sizeof(SzEither));
  e->is_right = 1;
  e->as.right = value;
  return e;
}

SzEither *sz_either_left(SzError *err) {
  SzEither *e = (SzEither *)sz_alloc_zero(sizeof(SzEither));
  e->is_right = 0;
  e->as.left = err;
  return e;
}

void sz_either_free(SzEither *e) {
  if (!e)
    return;
  if (!e->is_right && e->as.left)
    sz_error_free(e->as.left);
  sz_free(e);
}

SzAdt *sz_adt_new(int32_t tag, void *payload) {
  SzAdt *a = (SzAdt *)sz_alloc(sizeof(SzAdt));
  a->tag = tag;
  a->payload = payload;
  return a;
}

int32_t sz_adt_tag(const SzAdt *adt) { return adt ? adt->tag : -1; }

void *sz_adt_payload(const SzAdt *adt) { return adt ? adt->payload : NULL; }

void sz_adt_free(SzAdt *adt) { sz_free(adt); }

SzPair *sz_pair_new(void *left, void *right) {
  SzPair *p = (SzPair *)sz_alloc(sizeof(SzPair));
  p->left = left;
  p->right = right;
  return p;
}

void sz_pair_free(SzPair *p) { sz_free(p); }

/* --- IO constructors ----------------------------------------------------- */

static SzIo *sz_io_new(SzIoTag tag) {
  SzIo *io = (SzIo *)sz_alloc_zero(sizeof(SzIo));
  io->tag = tag;
  return io;
}

SzIo *sz_io_pure(void *value) {
  SzIo *io = sz_io_new(SZ_IO_PURE);
  io->as.pure_value = value;
  return io;
}

SzIo *sz_io_delay(SzThunk thunk, void *env) {
  if (!thunk)
    sz_panic("sz_io_delay(null thunk)");
  SzIo *io = sz_io_new(SZ_IO_DELAY);
  io->as.delay.thunk = thunk;
  io->as.delay.env = env;
  return io;
}

SzIo *sz_io_flatmap(SzIo *inner, SzCont cont, void *env) {
  if (!inner || !cont)
    sz_panic("sz_io_flatmap(null)");
  SzIo *io = sz_io_new(SZ_IO_FLATMAP);
  io->as.flatmap.inner = inner;
  io->as.flatmap.cont = cont;
  io->as.flatmap.env = env;
  return io;
}

SzIo *sz_io_fail(SzError *err) {
  SzIo *io = sz_io_new(SZ_IO_FAIL);
  io->as.fail = err;
  return io;
}

SzIo *sz_io_fail_cstr(const char *msg) {
  return sz_io_fail(sz_error_new(1, msg ? msg : "fail"));
}

SzIo *sz_io_println(SzString *msg) {
  if (!msg)
    sz_panic("sz_io_println(null)");
  SzIo *io = sz_io_new(SZ_IO_PRINTLN);
  io->as.println = msg;
  return io;
}

SzIo *sz_io_println_cstr(const char *msg) {
  return sz_io_println(sz_string_from_cstr(msg));
}

SzIo *sz_io_handle_error_with(SzIo *inner, SzErrorHandler handler, void *env) {
  if (!inner || !handler)
    sz_panic("sz_io_handle_error_with(null)");
  SzIo *io = sz_io_new(SZ_IO_HANDLE_ERROR);
  io->as.handle_error.inner = inner;
  io->as.handle_error.handler = handler;
  io->as.handle_error.env = env;
  return io;
}

SzIo *sz_io_attempt(SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_attempt(null)");
  SzIo *io = sz_io_new(SZ_IO_ATTEMPT);
  io->as.attempt_inner = inner;
  return io;
}

SzIo *sz_io_sleep_ms(int64_t ms) {
  SzIo *io = sz_io_new(SZ_IO_SLEEP_MS);
  io->as.sleep_ms = ms < 0 ? 0 : ms;
  return io;
}

SzIo *sz_io_race(SzIo *left, SzIo *right) {
  if (!left || !right)
    sz_panic("sz_io_race(null)");
  SzIo *io = sz_io_new(SZ_IO_RACE);
  io->as.race.left = left;
  io->as.race.right = right;
  return io;
}

SzIo *sz_io_both(SzIo *left, SzIo *right) {
  if (!left || !right)
    sz_panic("sz_io_both(null)");
  SzIo *io = sz_io_new(SZ_IO_BOTH);
  io->as.both.left = left;
  io->as.both.right = right;
  return io;
}

SzIo *sz_io_queue_take(SzQueue *q) {
  SzIo *io = sz_io_new(SZ_IO_QUEUE_TAKE);
  io->as.queue_take = q;
  return io;
}

SzIo *sz_io_deferred_get(SzDeferred *d) {
  SzIo *io = sz_io_new(SZ_IO_DEFERRED_GET);
  io->as.deferred_get = d;
  return io;
}

void sz_io_free(SzIo *io) {
  /* Shallow free; graphs are short-lived process heaps. */
  sz_free(io);
}

/* --- cooperative fiber scheduler (single-threaded, deterministic) -------- */

typedef enum ContKind { CONT_FLATMAP = 1, CONT_HANDLE = 2 } ContKind;

typedef struct ContFrame {
  ContKind kind;
  SzCont cont;
  SzErrorHandler handler;
  void *env;
  struct ContFrame *next;
} ContFrame;

typedef enum FiberState {
  FIB_READY = 1,
  FIB_SLEEP,
  FIB_QWAIT,
  FIB_DWAIT,
  FIB_JOIN,
  FIB_DONE,
  FIB_CANCELLED
} FiberState;

typedef enum JoinKind { JOIN_NONE = 0, JOIN_RACE = 1, JOIN_BOTH = 2 } JoinKind;

typedef struct Fiber {
  ContFrame *stack;
  SzIo *cur;
  FiberState state;
  int64_t wake_at;
  SzQueue *qwait;
  SzDeferred *dwait;
  struct Fiber *parent;
  JoinKind join_kind;
  int child_slot; /* 0 left, 1 right */
  struct Fiber *children[2];
  int child_ok[2];
  void *child_val[2];
  SzError *child_err[2];
  int children_settled;
  int result_ok;
  void *result_value;
  SzError *result_error;
  struct Fiber *ready_next;
  struct Fiber *wait_next;
} Fiber;

typedef struct Sched {
  Fiber *ready_head;
  Fiber *ready_tail;
  Fiber *sleepers;
  Fiber *root;
  Fiber *current;
} Sched;

static Sched *g_sched = NULL;

static ContFrame *cont_push_flatmap(ContFrame *stack, SzCont cont, void *env) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_FLATMAP;
  f->cont = cont;
  f->handler = NULL;
  f->env = env;
  f->next = stack;
  return f;
}

static ContFrame *cont_push_handle(ContFrame *stack, SzErrorHandler handler,
                                   void *env) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_HANDLE;
  f->cont = NULL;
  f->handler = handler;
  f->env = env;
  f->next = stack;
  return f;
}

static ContFrame *cont_pop(ContFrame *stack) {
  ContFrame *next = stack->next;
  sz_free(stack);
  return next;
}

static void cont_free_all(ContFrame *stack) {
  while (stack) {
    ContFrame *n = stack->next;
    sz_free(stack);
    stack = n;
  }
}

/* Attempt success continuation: wrap value as Right. */
static SzIo *attempt_ok(void *value, void *env) {
  (void)env;
  return sz_io_pure(sz_either_right(value));
}

static SzIo *attempt_err(SzError *err, void *env) {
  (void)env;
  return sz_io_pure(sz_either_left(err));
}

static void ready_enqueue(Sched *s, Fiber *f) {
  if (!f || f->state == FIB_CANCELLED || f->state == FIB_DONE)
    return;
  f->state = FIB_READY;
  f->ready_next = NULL;
  if (!s->ready_tail) {
    s->ready_head = f;
    s->ready_tail = f;
  } else {
    s->ready_tail->ready_next = f;
    s->ready_tail = f;
  }
}

static Fiber *ready_dequeue(Sched *s) {
  Fiber *f = s->ready_head;
  if (!f)
    return NULL;
  s->ready_head = f->ready_next;
  if (!s->ready_head)
    s->ready_tail = NULL;
  f->ready_next = NULL;
  return f;
}

static void sleeper_add(Sched *s, Fiber *f) {
  f->wait_next = s->sleepers;
  s->sleepers = f;
}

static void sleeper_remove(Sched *s, Fiber *f) {
  Fiber **pp = &s->sleepers;
  while (*pp) {
    if (*pp == f) {
      *pp = f->wait_next;
      f->wait_next = NULL;
      return;
    }
    pp = &(*pp)->wait_next;
  }
}

static void queue_waiter_add(SzQueue *q, Fiber *f) {
  f->wait_next = (Fiber *)q->waiters;
  q->waiters = f;
}

static void queue_waiter_remove(SzQueue *q, Fiber *f) {
  Fiber **pp = (Fiber **)&q->waiters;
  while (*pp) {
    if (*pp == f) {
      *pp = f->wait_next;
      f->wait_next = NULL;
      return;
    }
    pp = &(*pp)->wait_next;
  }
}

static void def_waiter_add(SzDeferred *d, Fiber *f) {
  f->wait_next = (Fiber *)d->waiters;
  d->waiters = f;
}

static void def_waiter_remove(SzDeferred *d, Fiber *f) {
  Fiber **pp = (Fiber **)&d->waiters;
  while (*pp) {
    if (*pp == f) {
      *pp = f->wait_next;
      f->wait_next = NULL;
      return;
    }
    pp = &(*pp)->wait_next;
  }
}

static Fiber *fiber_new(SzIo *cur, Fiber *parent, JoinKind jk, int slot) {
  Fiber *f = (Fiber *)sz_alloc_zero(sizeof(Fiber));
  f->cur = cur;
  f->state = FIB_READY;
  f->parent = parent;
  f->join_kind = jk;
  f->child_slot = slot;
  return f;
}

static void fiber_cancel(Sched *s, Fiber *f) {
  if (!f || f->state == FIB_DONE || f->state == FIB_CANCELLED)
    return;
  if (f->state == FIB_SLEEP)
    sleeper_remove(s, f);
  if (f->state == FIB_QWAIT && f->qwait)
    queue_waiter_remove(f->qwait, f);
  if (f->state == FIB_DWAIT && f->dwait)
    def_waiter_remove(f->dwait, f);
  if (f->children[0])
    fiber_cancel(s, f->children[0]);
  if (f->children[1])
    fiber_cancel(s, f->children[1]);
  f->state = FIB_CANCELLED;
  cont_free_all(f->stack);
  f->stack = NULL;
  f->cur = NULL;
}

static void fiber_resume_value(Sched *s, Fiber *f, void *value);
static void fiber_fail(Sched *s, Fiber *f, SzError *err);

static void join_child_done(Sched *s, Fiber *child, int ok, void *val,
                            SzError *err) {
  Fiber *p = child->parent;
  int slot;
  if (!p || p->state == FIB_CANCELLED || p->state == FIB_DONE)
    return;
  slot = child->child_slot;
  if (slot < 0 || slot > 1)
    return;
  p->child_ok[slot] = ok;
  p->child_val[slot] = val;
  p->child_err[slot] = err;
  p->children_settled += 1;

  if (p->join_kind == JOIN_RACE) {
    if (ok) {
      Fiber *sib = p->children[1 - slot];
      if (sib)
        fiber_cancel(s, sib);
      p->state = FIB_READY;
      p->cur = sz_io_pure(val);
      p->join_kind = JOIN_NONE;
      ready_enqueue(s, p);
      return;
    }
    if (p->children_settled >= 2) {
      SzError *e0 = p->child_err[0];
      SzError *e1 = p->child_err[1];
      SzError *out = e1 ? e1 : (e0 ? e0 : sz_error_new(6, "race both failed"));
      if (e0 && e0 != out)
        sz_error_free(e0);
      if (e1 && e1 != out)
        sz_error_free(e1);
      p->join_kind = JOIN_NONE;
      fiber_fail(s, p, out);
    }
    return;
  }

  if (p->join_kind == JOIN_BOTH) {
    if (!ok) {
      Fiber *sib = p->children[1 - slot];
      if (sib)
        fiber_cancel(s, sib);
      if (p->child_err[1 - slot] && p->child_err[1 - slot] != err)
        sz_error_free(p->child_err[1 - slot]);
      p->join_kind = JOIN_NONE;
      fiber_fail(s, p, err ? err : sz_error_new(7, "both failed"));
      return;
    }
    if (p->children_settled >= 2) {
      p->join_kind = JOIN_NONE;
      p->state = FIB_READY;
      p->cur = sz_io_pure(sz_pair_new(p->child_val[0], p->child_val[1]));
      ready_enqueue(s, p);
    }
  }
}

static void fiber_finish(Sched *s, Fiber *f, int ok, void *val, SzError *err) {
  if (f->state == FIB_CANCELLED)
    return;
  cont_free_all(f->stack);
  f->stack = NULL;
  f->cur = NULL;
  f->state = FIB_DONE;
  f->result_ok = ok;
  f->result_value = val;
  f->result_error = err;
  if (f->parent && f->parent->join_kind != JOIN_NONE)
    join_child_done(s, f, ok, val, err);
}

static void fiber_resume_value(Sched *s, Fiber *f, void *value) {
  ContFrame *stack = f->stack;
  while (stack && stack->kind == CONT_HANDLE)
    stack = cont_pop(stack);
  f->stack = stack;
  if (!stack) {
    fiber_finish(s, f, 1, value, NULL);
    return;
  }
  {
    SzCont cont = stack->cont;
    void *env = stack->env;
    f->stack = cont_pop(stack);
    f->cur = cont(value, env);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL,
                   sz_error_new(2, "flatMap continuation returned null"));
      return;
    }
    ready_enqueue(s, f);
  }
}

static void fiber_fail(Sched *s, Fiber *f, SzError *err) {
  ContFrame *stack = f->stack;
  while (stack) {
    if (stack->kind == CONT_HANDLE) {
      SzErrorHandler handler = stack->handler;
      void *env = stack->env;
      f->stack = cont_pop(stack);
      f->cur = handler(err, env);
      if (!f->cur) {
        fiber_finish(s, f, 0, NULL,
                     sz_error_new(2, "error handler returned null"));
        return;
      }
      ready_enqueue(s, f);
      return;
    }
    stack = cont_pop(stack);
    f->stack = stack;
  }
  fiber_finish(s, f, 0, NULL, err);
}

int sz_fiber_wake_queue(SzQueue *q, void *value) {
  Fiber *f;
  Sched *s = g_sched;
  if (!q || !s || !q->waiters)
    return 0;
  f = (Fiber *)q->waiters;
  q->waiters = f->wait_next;
  f->wait_next = NULL;
  f->qwait = NULL;
  f->state = FIB_READY;
  f->cur = sz_io_pure(value);
  ready_enqueue(s, f);
  return 1;
}

void sz_fiber_wake_deferred(SzDeferred *d) {
  Sched *s = g_sched;
  Fiber *f;
  if (!d || !s)
    return;
  while (d->waiters) {
    f = (Fiber *)d->waiters;
    d->waiters = f->wait_next;
    f->wait_next = NULL;
    f->dwait = NULL;
    if (f->state == FIB_CANCELLED)
      continue;
    if (!d->ok) {
      SzError *err =
          d->error
              ? sz_error_new(d->error->code, sz_string_cstr(d->error->message))
              : sz_error_new(1, "deferred failed");
      f->state = FIB_READY;
      f->stack = f->stack; /* keep handlers */
      f->cur = sz_io_fail(err);
      ready_enqueue(s, f);
    } else {
      f->state = FIB_READY;
      f->cur = sz_io_pure(d->value);
      ready_enqueue(s, f);
    }
  }
}

static void park_sleep(Sched *s, Fiber *f, int64_t ms) {
  int64_t now = sz_clock_monotonic_ms_sync();
  f->wake_at = now + (ms < 0 ? 0 : ms);
  f->state = FIB_SLEEP;
  f->cur = NULL;
  sleeper_add(s, f);
}

static int step_fiber(Sched *s, Fiber *f) {
  SzIo *cur;
  if (f->state == FIB_CANCELLED || f->state == FIB_DONE || f->state == FIB_JOIN)
    return 0;
  cur = f->cur;
  if (!cur) {
    fiber_finish(s, f, 0, NULL, sz_error_new(1, "null IO"));
    return 0;
  }
  s->current = f;

  switch (cur->tag) {
  case SZ_IO_PURE:
    fiber_resume_value(s, f, cur->as.pure_value);
    return 0;
  case SZ_IO_DELAY: {
    void *value = cur->as.delay.thunk(cur->as.delay.env);
    f->cur = sz_io_pure(value);
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_PRINTLN: {
    const char *str = sz_string_cstr(cur->as.println);
    if (sz_testrt_sys_is_fake())
      sz_testrt_stdout_append(str);
    fputs(str, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    f->cur = sz_io_pure(NULL);
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_SLEEP_MS:
    park_sleep(s, f, cur->as.sleep_ms);
    return 0;
  case SZ_IO_FAIL: {
    SzError *err =
        cur->as.fail ? cur->as.fail : sz_error_new(3, "unknown failure");
    fiber_fail(s, f, err);
    return 0;
  }
  case SZ_IO_FLATMAP:
    f->stack =
        cont_push_flatmap(f->stack, cur->as.flatmap.cont, cur->as.flatmap.env);
    f->cur = cur->as.flatmap.inner;
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL, sz_error_new(5, "flatMap inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  case SZ_IO_HANDLE_ERROR:
    f->stack = cont_push_handle(f->stack, cur->as.handle_error.handler,
                                cur->as.handle_error.env);
    f->cur = cur->as.handle_error.inner;
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL,
                   sz_error_new(5, "handleErrorWith inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  case SZ_IO_ATTEMPT: {
    SzIo *inner = cur->as.attempt_inner;
    SzIo *mapped = sz_io_flatmap(inner, attempt_ok, NULL);
    f->cur = sz_io_handle_error_with(mapped, attempt_err, NULL);
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_RACE: {
    Fiber *left = fiber_new(cur->as.race.left, f, JOIN_RACE, 0);
    Fiber *right = fiber_new(cur->as.race.right, f, JOIN_RACE, 1);
    f->children[0] = left;
    f->children[1] = right;
    f->children_settled = 0;
    f->join_kind = JOIN_RACE;
    f->state = FIB_JOIN;
    f->cur = NULL;
    ready_enqueue(s, left);
    ready_enqueue(s, right);
    return 0;
  }
  case SZ_IO_BOTH: {
    Fiber *left = fiber_new(cur->as.both.left, f, JOIN_BOTH, 0);
    Fiber *right = fiber_new(cur->as.both.right, f, JOIN_BOTH, 1);
    f->children[0] = left;
    f->children[1] = right;
    f->children_settled = 0;
    f->join_kind = JOIN_BOTH;
    f->state = FIB_JOIN;
    f->cur = NULL;
    ready_enqueue(s, left);
    ready_enqueue(s, right);
    return 0;
  }
  case SZ_IO_QUEUE_TAKE: {
    SzQueue *q = cur->as.queue_take;
    if (!q) {
      fiber_finish(s, f, 0, NULL, sz_error_new(1, "null queue"));
      return 0;
    }
    if (q->len > 0) {
      void *v = q->items[0];
      size_t i;
      for (i = 1; i < q->len; i++)
        q->items[i - 1] = q->items[i];
      q->len--;
      f->cur = sz_io_pure(v);
      ready_enqueue(s, f);
      return 0;
    }
    f->state = FIB_QWAIT;
    f->qwait = q;
    f->cur = NULL;
    queue_waiter_add(q, f);
    return 0;
  }
  case SZ_IO_DEFERRED_GET: {
    SzDeferred *d = cur->as.deferred_get;
    if (!d) {
      fiber_finish(s, f, 0, NULL, sz_error_new(1, "null deferred"));
      return 0;
    }
    if (d->completed) {
      if (!d->ok) {
        SzError *err =
            d->error
                ? sz_error_new(d->error->code, sz_string_cstr(d->error->message))
                : sz_error_new(1, "deferred failed");
        fiber_fail(s, f, err);
        return 0;
      }
      f->cur = sz_io_pure(d->value);
      ready_enqueue(s, f);
      return 0;
    }
    f->state = FIB_DWAIT;
    f->dwait = d;
    f->cur = NULL;
    def_waiter_add(d, f);
    return 0;
  }
  default:
    fiber_finish(s, f, 0, NULL, sz_error_new(4, "invalid IO tag"));
    return 0;
  }
}

static int wake_sleepers(Sched *s, int64_t now) {
  Fiber *f = s->sleepers;
  Fiber *next;
  int woke = 0;
  s->sleepers = NULL;
  while (f) {
    next = f->wait_next;
    f->wait_next = NULL;
    if (f->state == FIB_CANCELLED) {
      f = next;
      continue;
    }
    if (f->state == FIB_SLEEP && f->wake_at <= now) {
      f->cur = sz_io_pure(NULL);
      ready_enqueue(s, f);
      woke = 1;
    } else if (f->state == FIB_SLEEP) {
      f->wait_next = s->sleepers;
      s->sleepers = f;
    }
    f = next;
  }
  return woke;
}

static int64_t next_wake_at(Sched *s) {
  Fiber *f = s->sleepers;
  int64_t best = -1;
  while (f) {
    if (f->state == FIB_SLEEP && (best < 0 || f->wake_at < best))
      best = f->wake_at;
    f = f->wait_next;
  }
  return best;
}

static int idle_advance(Sched *s) {
  int64_t now = sz_clock_monotonic_ms_sync();
  int64_t next = next_wake_at(s);
  int64_t delta;
  if (next < 0)
    return 0;
  if (next <= now)
    return wake_sleepers(s, now);
  delta = next - now;
  if (sz_testrt_clock_is_fake()) {
    sz_testrt_clock_advance(delta);
  } else {
    struct timespec ts;
    ts.tv_sec = (time_t)(delta / 1000);
    ts.tv_nsec = (long)((delta % 1000) * 1000000L);
    nanosleep(&ts, NULL);
  }
  now = sz_clock_monotonic_ms_sync();
  return wake_sleepers(s, now);
}

static SzIoResult run_io(SzIo *root) {
  Sched sched;
  SzIoResult result;
  result.ok = 0;
  result.value = NULL;
  result.error = NULL;

  if (!root) {
    result.error = sz_error_new(1, "null IO");
    return result;
  }

  memset(&sched, 0, sizeof(sched));
  sched.root = fiber_new(root, NULL, JOIN_NONE, 0);
  ready_enqueue(&sched, sched.root);
  g_sched = &sched;

  for (;;) {
    Fiber *f = ready_dequeue(&sched);
    if (f) {
      if (f->state == FIB_CANCELLED || f->state == FIB_DONE)
        continue;
      step_fiber(&sched, f);
      if (sched.root->state == FIB_DONE || sched.root->state == FIB_CANCELLED)
        break;
      continue;
    }
    if (idle_advance(&sched))
      continue;
    /* Deadlock: no ready fibers and nothing to wake. */
    break;
  }

  g_sched = NULL;
  if (sched.root->state == FIB_DONE && sched.root->result_ok) {
    result.ok = 1;
    result.value = sched.root->result_value;
  } else if (sched.root->state == FIB_DONE) {
    result.ok = 0;
    result.error = sched.root->result_error
                       ? sched.root->result_error
                       : sz_error_new(1, "IO failed");
  } else {
    fiber_cancel(&sched, sched.root);
    result.ok = 0;
    result.error = sz_error_new(8, "IO deadlock (parked with no wakeup)");
  }

  return result;
}

SzIoResult sz_io_unsafe_run(SzIo *root) { return run_io(root); }

int sz_runtime_main(SzIo *program) {
  return sz_runtime_main_args(program, 0, NULL);
}

typedef struct {
  SzIo *program;
  int argc;
  char **argv;
  int rc;
} SzMainArgs;

#if defined(__APPLE__)
/* Worker finished — main thread may leave the CFRunLoop park. */
static volatile int g_sz_main_worker_done;
#endif

static void *sz_runtime_main_worker(void *arg) {
  SzMainArgs *a = (SzMainArgs *)arg;
  if (a->argc > 0 && a->argv)
    sz_sys_set_args(a->argc, a->argv);
  {
    const char *tr = getenv("SCUZZ_TESTRT");
    if (tr && tr[0] == '1')
      sz_testrt_install();
  }
  SzIoResult r = sz_io_unsafe_run(a->program);
  if (!r.ok) {
    fprintf(stderr, "scuzz: IO failed: %s\n",
            r.error ? sz_string_cstr(r.error->message) : "unknown");
    if (r.error)
      sz_error_free(r.error);
    a->rc = 1;
  } else {
    a->rc = 0;
  }
#if defined(__APPLE__)
  g_sz_main_worker_done = 1;
  /* Wake the main CFRunLoop so it notices the done flag promptly. */
  CFRunLoopStop(CFRunLoopGetMain());
#endif
  return NULL;
}

int sz_runtime_main_args(SzIo *program, int argc, char **argv) {
  /* Run the program on a heap-allocated stack so Stage-1 emit (deep but
   * bounded) does not depend on the process main-thread ulimit — required on
   * macOS where `ulimit -s` cannot grow the main stack. */
  SzMainArgs args;
  pthread_t thr;
  pthread_attr_t attr;
  size_t stack = 64u * 1024u * 1024u;
  int perr;

  args.program = program;
  args.argc = argc;
  args.argv = argv;
  args.rc = 1;

  perr = pthread_attr_init(&attr);
  if (perr != 0)
    sz_panic("pthread_attr_init failed");
  perr = pthread_attr_setstacksize(&attr, stack);
  if (perr != 0) {
    pthread_attr_destroy(&attr);
    /* Fall back to the calling thread if the platform rejects the size. */
    sz_runtime_main_worker(&args);
    return args.rc;
  }
#if defined(__APPLE__)
  g_sz_main_worker_done = 0;
#endif
  perr = pthread_create(&thr, &attr, sz_runtime_main_worker, &args);
  pthread_attr_destroy(&attr);
  if (perr != 0) {
    sz_runtime_main_worker(&args);
    return args.rc;
  }
#if defined(__APPLE__)
  /* Keep the process main thread in the CFRunLoop so AppKit work (NSWindow)
   * dispatched from the worker can run. A plain pthread_join would deadlock
   * with dispatch_sync to the main queue. */
  while (!g_sz_main_worker_done) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
  }
#endif
  perr = pthread_join(thr, NULL);
  if (perr != 0)
    sz_panic("pthread_join failed");
  return args.rc;
}
