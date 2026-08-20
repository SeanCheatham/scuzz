#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <errno.h>
#include <stdint.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SZ_ASAN 1
#endif
#endif
#ifdef __SANITIZE_ADDRESS__
#define SZ_ASAN 1
#endif
#ifdef SZ_ASAN
void *__asan_region_is_poisoned(void *beg, size_t size);
#endif

static SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}


static SzIo *pure_drop(void *value) {
  SzIo *io = sz_io_pure(value);
  sz_release(value);
  return io;
}

static SzIo *fail_drop(SzError *err) {
  SzIo *io = sz_io_fail(err);
  sz_release(err);
  return io;
}

static SzEither *either_left_drop(SzError *err) {
  SzEither *e = sz_either_left(err);
  sz_release(err);
  return e;
}

static SzEither *either_right_drop(void *value) {
  SzEither *e = sz_either_right(value);
  sz_release(value);
  return e;
}

static SzIo *attempt_drop(SzIo *inner) {
  SzIo *io = sz_io_attempt(inner);
  sz_release(inner);
  return io;
}

/* --- panic / alloc ------------------------------------------------------- */

/* Header before every user pointer: [size_t nbytes][SzRcHdr][user].
 * RC objects set SZ_RC_MAGIC. Other sz_alloc objects set SZ_ALLOC_MAGIC so
 * sz_retain / sz_release can read the header without leaving the block. */
#define SZ_ALLOC_MAGIC 0x535A414Cu /* 'SZAL' */
#define SZ_RC_MAGIC 0x535A5243u    /* 'SZRC' */

typedef struct SzRcHdr {
  uint32_t magic;
  uint32_t rc;
  uint32_t kind;
  uint32_t pad;
  struct SzRcHdr *prev;
  struct SzRcHdr *next;
} SzRcHdr;

static SzRcHdr *g_live = NULL;
static int g_in_panic = 0;
static char g_panic_dump[1024];
enum { SZ_LIVE_DUMP_ROWS = 32, SZ_LIVE_PANIC_ROWS = 64 };

static size_t hdr_bytes(const SzRcHdr *h) { return *(((size_t *)h) - 1); }

static void live_link(SzRcHdr *h) {
  h->prev = NULL;
  h->next = g_live;
  if (g_live)
    g_live->prev = h;
  g_live = h;
}

static void live_unlink(SzRcHdr *h) {
  if (h->prev)
    h->prev->next = h->next;
  else if (g_live == h)
    g_live = h->next;
  if (h->next)
    h->next->prev = h->prev;
  h->prev = NULL;
  h->next = NULL;
}

static SzRcHdr *sz_rc_hdr(const void *ptr) {
  return ((SzRcHdr *)ptr) - 1;
}

static int sz_hdr_readable(const void *ptr) {
#ifdef SZ_ASAN
  SzRcHdr *h = sz_rc_hdr(ptr);
  if (__asan_region_is_poisoned((void *)h, sizeof(*h)))
    return 0;
#else
  (void)ptr;
#endif
  return 1;
}

static int sz_is_rc(const void *ptr) {
  uintptr_t p = (uintptr_t)ptr;
  /* Small integers are not RC. Do not load a header. */
  if (p < 4096 || (p & 7) != 0)
    return 0;
  if (!sz_hdr_readable(ptr))
    return 0;
  return sz_rc_hdr(ptr)->magic == SZ_RC_MAGIC;
}

static int sz_is_alloc(const void *ptr) {
  uintptr_t p = (uintptr_t)ptr;
  if (p < 4096 || (p & 7) != 0)
    return 0;
  if (!sz_hdr_readable(ptr))
    return 0;
  return sz_rc_hdr(ptr)->magic == SZ_ALLOC_MAGIC;
}

static size_t g_live_bytes = 0;
static size_t g_live_count = 0;
static size_t g_peak_bytes = 0;
static size_t g_mark_bytes = 0;
static size_t g_mark_count = 0;
static size_t g_kind_bytes[SZ_RC_KIND_COUNT];
static size_t g_kind_count[SZ_RC_KIND_COUNT];
static unsigned g_trace_pumps = 0;
enum { SZ_ALLOC_TRACE_EVERY = 32 };

static const char *k_kind_names[SZ_RC_KIND_COUNT] = {
    "raw",      "string", "list",     "adt",      "box",  "map",
    "io",       "stream", "resource", "error",    "ref",  "queue",
    "deferred", "either", "pair"};

static uint32_t kind_idx(uint32_t kind) {
  return kind < SZ_RC_KIND_COUNT ? kind : (uint32_t)SZ_RC_RAW;
}

static void kind_add(uint32_t kind, size_t n) {
  uint32_t i = kind_idx(kind);
  g_kind_bytes[i] += n;
  g_kind_count[i] += 1;
}

static void kind_sub(uint32_t kind, size_t n) {
  uint32_t i = kind_idx(kind);
  if (g_kind_count[i] > 0)
    g_kind_count[i] -= 1;
  if (g_kind_bytes[i] >= n)
    g_kind_bytes[i] -= n;
  else
    g_kind_bytes[i] = 0;
}

static void heap_append(char *buf, size_t cap, size_t *n, const char *fmt, ...) {
  va_list ap;
  int w;
  if (!buf || cap == 0 || *n >= cap)
    return;
  va_start(ap, fmt);
  w = vsnprintf(buf + *n, cap - *n, fmt, ap);
  va_end(ap);
  if (w < 0)
    return;
  if ((size_t)w >= cap - *n)
    *n = cap - 1;
  else
    *n += (size_t)w;
}

static void *alloc_block(size_t size, int zero, uint32_t magic, uint32_t kind) {
  size_t *raw;
  SzRcHdr *h;
  if (zero)
    raw = (size_t *)calloc(1, sizeof(size_t) + sizeof(SzRcHdr) + size);
  else
    raw = (size_t *)malloc(sizeof(size_t) + sizeof(SzRcHdr) + size);
  if (!raw)
    sz_panic("out of memory");
  *raw = size;
  h = (SzRcHdr *)(raw + 1);
  h->magic = magic;
  h->rc = (magic == SZ_RC_MAGIC) ? 1 : 0;
  h->kind = kind;
  h->pad = 0;
  h->prev = NULL;
  h->next = NULL;
  live_link(h);
  g_live_bytes += size;
  g_live_count += 1;
  kind_add(kind, size);
  if (g_live_bytes > g_peak_bytes)
    g_peak_bytes = g_live_bytes;
  return (void *)(h + 1);
}

static const char *panic_dump_path(void) {
  const char *e;
  if (g_panic_dump[0])
    return g_panic_dump;
  e = getenv("SCUZZ_PANIC_DUMP");
  if (e && e[0])
    return e;
  return NULL;
}

void sz_alloc_set_panic_dump(const char *path) {
  if (!path || !path[0]) {
    g_panic_dump[0] = '\0';
    return;
  }
  snprintf(g_panic_dump, sizeof g_panic_dump, "%s", path);
}

const char *sz_alloc_panic_dump_path(void) { return panic_dump_path(); }

void sz_alloc_walk(void (*fn)(void *ptr, uint32_t kind, size_t bytes, uint32_t rc,
                              void *ctx),
                   void *ctx) {
  SzRcHdr *h;
  if (!fn)
    return;
  for (h = g_live; h; h = h->next)
    fn((void *)(h + 1), h->kind, hdr_bytes(h), h->rc, ctx);
}

int sz_alloc_format_live(char *buf, size_t cap, int max_rows) {
  SzRcHdr *h;
  size_t n = 0;
  int shown = 0;
  int extra = 0;
  if (!buf || cap == 0)
    return 0;
  buf[0] = '\0';
  for (h = g_live; h; h = h->next) {
    if (max_rows >= 0 && shown >= max_rows) {
      extra += 1;
      continue;
    }
    heap_append(buf, cap, &n, "%s rc=%u bytes=%zu\n",
                sz_alloc_kind_name(h->kind), h->rc, hdr_bytes(h));
    shown += 1;
  }
  if (extra > 0)
    heap_append(buf, cap, &n, "truncated=%d\n", extra);
  return (int)n;
}

int sz_alloc_format_panic(char *buf, size_t cap, const char *msg) {
  size_t n = 0;
  char heap[1536];
  char live[4096];
  if (!buf || cap == 0)
    return 0;
  buf[0] = '\0';
  heap_append(buf, cap, &n, "scuzz panic: %s\n", msg ? msg : "(null)");
  sz_alloc_format_heap(heap, sizeof heap, 0);
  heap_append(buf, cap, &n, "[heap]\n%s", heap);
  sz_alloc_format_live(live, sizeof live, SZ_LIVE_PANIC_ROWS);
  heap_append(buf, cap, &n, "[live]\n%s", live);
  return (int)n;
}

void sz_alloc_sweep(void) {
  while (g_live)
    sz_free((void *)(g_live + 1));
}

void sz_panic(const char *msg) {
  char report[8192];
  const char *path;
  FILE *f;
  if (g_in_panic)
    abort();
  g_in_panic = 1;
  sz_alloc_format_panic(report, sizeof report, msg);
  fputs(report, stderr);
  fflush(stderr);
  path = panic_dump_path();
  if (path) {
    f = fopen(path, "w");
    if (f) {
      fputs(report, f);
      fclose(f);
    }
  }
  sz_alloc_sweep();
  abort();
}

void *sz_alloc(size_t size) {
  return alloc_block(size, 0, SZ_ALLOC_MAGIC, SZ_RC_RAW);
}

void *sz_alloc_zero(size_t size) {
  return alloc_block(size, 1, SZ_ALLOC_MAGIC, SZ_RC_RAW);
}

void sz_free(void *ptr) {
  SzRcHdr *h;
  size_t *raw;
  size_t n;
  if (!ptr)
    return;
  h = ((SzRcHdr *)ptr) - 1;
  raw = ((size_t *)h) - 1;
  n = *raw;
  live_unlink(h);
  kind_sub(h->kind, n);
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

void sz_alloc_kind_stats(uint32_t kind, size_t *bytes, size_t *count) {
  uint32_t i = kind_idx(kind);
  if (bytes)
    *bytes = g_kind_bytes[i];
  if (count)
    *count = g_kind_count[i];
}

const char *sz_alloc_kind_name(uint32_t kind) {
  return k_kind_names[kind_idx(kind)];
}

size_t sz_alloc_peak_bytes(void) { return g_peak_bytes; }

void sz_alloc_reset_stats(void) {
  g_peak_bytes = g_live_bytes;
  g_trace_pumps = 0;
}

void sz_alloc_mark(void) {
  g_mark_bytes = g_live_bytes;
  g_mark_count = g_live_count;
}

void sz_alloc_delta(int64_t *bytes, int64_t *count) {
  if (bytes)
    *bytes = (int64_t)g_live_bytes - (int64_t)g_mark_bytes;
  if (count)
    *count = (int64_t)g_live_count - (int64_t)g_mark_count;
}

int sz_alloc_format_heap(char *buf, size_t cap, int mark) {
  int64_t db = 0, dc = 0;
  size_t n = 0;
  int i;
  if (!buf || cap == 0)
    return 0;
  buf[0] = '\0';
  sz_alloc_delta(&db, &dc);
  heap_append(buf, cap, &n,
              "live_bytes=%zu\nlive_count=%zu\npeak_bytes=%zu\n"
              "delta_bytes=%lld\ndelta_count=%lld\n",
              g_live_bytes, g_live_count, g_peak_bytes, (long long)db,
              (long long)dc);
  for (i = 0; i < SZ_RC_KIND_COUNT; i++)
    heap_append(buf, cap, &n, "%s=%zu:%zu\n", k_kind_names[i],
                g_kind_count[i], g_kind_bytes[i]);
  if (mark)
    sz_alloc_mark();
  return (int)n;
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

static void delay_env_drop(void *env) {
  if (sz_is_rc(env))
    sz_release(env);
  else if (sz_is_alloc(env))
    sz_free(env);
}

void *sz_rc_alloc(size_t size, uint32_t kind) {
  return alloc_block(size, 0, SZ_RC_MAGIC, kind);
}

void sz_retain(void *ptr) {
  if (!sz_is_rc(ptr))
    return;
  sz_rc_hdr(ptr)->rc += 1;
}

void sz_release(void *ptr) {
  SzRcHdr *h;
  uint32_t kind;
  if (!sz_is_rc(ptr))
    return;
  h = sz_rc_hdr(ptr);
  if (h->rc > 1) {
    h->rc -= 1;
    return;
  }
  kind = h->kind;
  h->magic = 0;
  h->rc = 0;
  switch (kind) {
  case SZ_RC_STRING: {
    SzString *s = (SzString *)ptr;
    sz_free(s->data);
    s->data = NULL;
    break;
  }
  case SZ_RC_LIST: {
    SzList *xs = (SzList *)ptr;
    for (;;) {
      void *head = xs->head;
      SzList *tail = xs->tail;
      SzRcHdr *cell = sz_rc_hdr(xs);
      xs->head = NULL;
      xs->tail = NULL;
      cell->magic = 0;
      sz_free(xs);
      sz_release(head);
      if (!sz_is_rc(tail))
        return;
      if (sz_rc_hdr(tail)->rc > 1) {
        sz_release(tail);
        return;
      }
      xs = tail;
    }
  }
  case SZ_RC_ADT: {
    SzAdt *a = (SzAdt *)ptr;
    void *payload = a->payload;
    a->payload = NULL;
    sz_free(ptr);
    sz_release(payload);
    return;
  }
  case SZ_RC_MAP: {
    SzMap *m = (SzMap *)ptr;
    void *k = m->key;
    void *v = m->val;
    SzMap *l = m->left;
    SzMap *r = m->right;
    m->key = NULL;
    m->val = NULL;
    m->left = NULL;
    m->right = NULL;
    sz_free(ptr);
    sz_release(k);
    sz_release(v);
    sz_release(l);
    sz_release(r);
    return;
  }
  case SZ_RC_IO: {
    SzIo *io = (SzIo *)ptr;
    switch (io->tag) {
    case SZ_IO_FLATMAP:
      sz_release(io->as.flatmap.inner);
      sz_release(io->as.flatmap.env);
      io->as.flatmap.inner = NULL;
      io->as.flatmap.env = NULL;
      break;
    case SZ_IO_HANDLE_ERROR:
      sz_release(io->as.handle_error.inner);
      sz_release(io->as.handle_error.env);
      io->as.handle_error.inner = NULL;
      io->as.handle_error.env = NULL;
      break;
    case SZ_IO_ATTEMPT:
      sz_release(io->as.attempt_inner);
      io->as.attempt_inner = NULL;
      break;
    case SZ_IO_PRINTLN:
      sz_release(io->as.println);
      io->as.println = NULL;
      break;
    case SZ_IO_RACE:
      sz_release(io->as.race.left);
      sz_release(io->as.race.right);
      io->as.race.left = NULL;
      io->as.race.right = NULL;
      break;
    case SZ_IO_BOTH:
      sz_release(io->as.both.left);
      sz_release(io->as.both.right);
      io->as.both.left = NULL;
      io->as.both.right = NULL;
      break;
    case SZ_IO_ENSURE:
      sz_release(io->as.ensure.inner);
      sz_release(io->as.ensure.finalizer);
      io->as.ensure.inner = NULL;
      io->as.ensure.finalizer = NULL;
      break;
    case SZ_IO_TIMEOUT:
      sz_release(io->as.timeout.inner);
      io->as.timeout.inner = NULL;
      break;
    case SZ_IO_FOREVER:
    case SZ_IO_REPEAT_N:
    case SZ_IO_RETRY_N:
      sz_release(io->as.loop.inner);
      io->as.loop.inner = NULL;
      break;
    case SZ_IO_FORK:
      sz_release(io->as.fork_inner);
      io->as.fork_inner = NULL;
      break;
    case SZ_IO_QUEUE_TAKE:
      sz_release(io->as.queue_take);
      io->as.queue_take = NULL;
      break;
    case SZ_IO_DEFERRED_GET:
      sz_release(io->as.deferred_get);
      io->as.deferred_get = NULL;
      break;
    case SZ_IO_PURE:
      sz_release(io->as.pure_value);
      io->as.pure_value = NULL;
      break;
    case SZ_IO_FAIL:
      sz_error_free(io->as.fail);
      io->as.fail = NULL;
      break;
    case SZ_IO_DELAY:
      delay_env_drop(io->as.delay.env);
      io->as.delay.env = NULL;
      break;
    default:
      break;
    }
    break;
  }
  case SZ_RC_STREAM: {
    SzStream *st = (SzStream *)ptr;
    for (;;) {
      int tag = st->tag;
      void *left = st->left;
      void *right = st->right;
      void *env = st->env;
      SzRcHdr *cell = sz_rc_hdr(st);
      st->left = NULL;
      st->right = NULL;
      st->env = NULL;
      cell->magic = 0;
      sz_free(st);
      if (tag == SZ_ST_CONS || tag == SZ_ST_EVAL) {
        sz_release(left);
        if (!sz_is_rc(right))
          return;
        if (sz_rc_hdr(right)->rc > 1) {
          sz_release(right);
          return;
        }
        st = (SzStream *)right;
        continue;
      }
      sz_release(left);
      if (tag == SZ_ST_CONCAT)
        sz_release(right);
      /* TAKE/DROP store a count in env. Filter and map nodes store a capture
       * list. Function pointers in right are not RC. */
      if (tag != SZ_ST_TAKE && tag != SZ_ST_DROP)
        sz_release(env);
      return;
    }
  }
  case SZ_RC_RESOURCE: {
    SzLangResource *r = (SzLangResource *)ptr;
    void *env = r->release_env;
    SzIo *acq = r->acquire;
    r->release_env = NULL;
    r->acquire = NULL;
    sz_free(ptr);
    sz_release(env);
    sz_release(acq);
    return;
  }
  case SZ_RC_ERROR: {
    SzError *e = (SzError *)ptr;
    SzString *msg = e->message;
    e->message = NULL;
    sz_free(ptr);
    sz_release(msg);
    return;
  }
  case SZ_RC_REF: {
    SzRef *r = (SzRef *)ptr;
    void *v = r->value;
    r->value = NULL;
    sz_free(ptr);
    sz_release(v);
    return;
  }
  case SZ_RC_QUEUE: {
    SzQueue *q = (SzQueue *)ptr;
    size_t i;
    for (i = 0; i < q->len; i++) {
      size_t idx = q->head + i;
      if (q->cap && idx >= q->cap)
        idx -= q->cap;
      sz_release(q->items[idx]);
    }
    sz_free(q->items);
    q->items = NULL;
    q->head = 0;
    q->len = 0;
    q->cap = 0;
    break;
  }
  case SZ_RC_DEFERRED: {
    SzDeferred *d = (SzDeferred *)ptr;
    int ok = d->completed && d->ok;
    int bad = d->completed && !d->ok;
    void *v = d->value;
    SzError *err = d->error;
    d->value = NULL;
    d->error = NULL;
    sz_free(ptr);
    if (ok)
      sz_release(v);
    else if (bad)
      sz_error_free(err);
    return;
  }
  case SZ_RC_EITHER: {
    SzEither *e = (SzEither *)ptr;
    if (e->is_right) {
      void *v = e->as.right;
      e->as.right = NULL;
      sz_free(ptr);
      sz_release(v);
    } else {
      SzError *err = e->as.left;
      e->as.left = NULL;
      sz_free(ptr);
      sz_release(err);
    }
    return;
  }
  case SZ_RC_PAIR: {
    SzPair *p = (SzPair *)ptr;
    void *l = p->left;
    void *r = p->right;
    p->left = NULL;
    p->right = NULL;
    sz_free(ptr);
    sz_release(l);
    sz_release(r);
    return;
  }
  case SZ_RC_BOX:
    break;
  default:
    break;
  }
  sz_free(ptr);
}

/* --- strings ------------------------------------------------------------- */

SzString *sz_string_from_bytes(const char *bytes, size_t len) {
  SzString *s = (SzString *)sz_rc_alloc(sizeof(SzString), SZ_RC_STRING);
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

void sz_string_free(SzString *s) { sz_release(s); }

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

SzString *sz_string_from_float(double x) {
  char buf[64];
  char *dot;
  char *end;
  if (x != x) {
    return sz_string_from_cstr("NaN");
  }
  snprintf(buf, sizeof(buf), "%.6f", x);
  dot = strchr(buf, '.');
  if (dot) {
    end = buf + strlen(buf);
    while (end > dot + 2 && end[-1] == '0') {
      end--;
      *end = '\0';
    }
  }
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

int64_t sz_string_last_index_of(const SzString *s, const SzString *needle) {
  size_t max;
  if (!s || !needle)
    return -1;
  if (needle->len == 0)
    return sz_string_len(s);
  if (needle->len > s->len)
    return -1;
  max = s->len - needle->len;
  for (size_t i = max + 1; i > 0; i--) {
    size_t at = i - 1;
    if (memcmp(s->data + at, needle->data, needle->len) == 0)
      return (int64_t)at;
  }
  return -1;
}

SzString *sz_string_take(const SzString *s, int64_t n) {
  if (n <= 0)
    return sz_string_from_cstr("");
  return sz_string_slice(s, 0, n);
}

SzString *sz_string_drop(const SzString *s, int64_t n) {
  int64_t len = sz_string_len(s);
  if (n <= 0)
    return sz_string_slice(s, 0, len);
  return sz_string_slice(s, n, len);
}

SzString *sz_string_take_right(const SzString *s, int64_t n) {
  int64_t len = sz_string_len(s);
  if (n <= 0)
    return sz_string_from_cstr("");
  if (n >= len)
    return sz_string_slice(s, 0, len);
  return sz_string_slice(s, len - n, len);
}

SzString *sz_string_drop_right(const SzString *s, int64_t n) {
  int64_t len = sz_string_len(s);
  if (n <= 0)
    return sz_string_slice(s, 0, len);
  if (n >= len)
    return sz_string_from_cstr("");
  return sz_string_slice(s, 0, len - n);
}

SzString *sz_string_reverse(const SzString *s) {
  size_t n = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  char *buf;
  size_t i;
  SzString *out;
  buf = (char *)sz_alloc(n + 1);
  for (i = 0; i < n; i++)
    buf[i] = src[n - 1 - i];
  buf[n] = '\0';
  out = sz_string_from_bytes(buf, n);
  sz_free(buf);
  return out;
}

int64_t sz_string_starts_with(const SzString *s, const SzString *prefix) {
  return sz_string_index_of(s, prefix) == 0 ? 1 : 0;
}

int64_t sz_string_contains(const SzString *s, const SzString *needle) {
  return sz_string_index_of(s, needle) >= 0 ? 1 : 0;
}

int64_t sz_string_ends_with(const SzString *s, const SzString *suffix) {
  size_t slen;
  size_t n;
  if (!suffix)
    return 0;
  n = suffix->len;
  if (n == 0)
    return 1;
  if (!s || n > s->len)
    return 0;
  slen = s->len;
  return memcmp(s->data + slen - n, suffix->data, n) == 0 ? 1 : 0;
}

int64_t sz_string_to_int(const SzString *s, int64_t dflt) {
  const char *p;
  char *end;
  long long v;
  if (!s || !s->data || s->len == 0)
    return dflt;
  errno = 0;
  p = s->data;
  v = strtoll(p, &end, 10);
  if (end == p || *end != '\0' || errno == ERANGE)
    return dflt;
  return (int64_t)v;
}

SzString *sz_string_replace(const SzString *s, const SzString *oldv, const SzString *newv) {
  const char *src;
  const char *oldp;
  const char *newp;
  size_t slen;
  size_t nold;
  size_t nnew;
  size_t count;
  size_t out_len;
  size_t i;
  size_t o;
  char *buf;
  SzString *out;
  slen = s && s->data ? s->len : 0;
  src = s && s->data ? s->data : "";
  nold = oldv && oldv->data ? oldv->len : 0;
  nnew = newv && newv->data ? newv->len : 0;
  oldp = oldv && oldv->data ? oldv->data : "";
  newp = newv && newv->data ? newv->data : "";
  if (nold == 0)
    return sz_string_from_bytes(src, slen);
  count = 0;
  i = 0;
  while (i + nold <= slen) {
    if (memcmp(src + i, oldp, nold) == 0) {
      count++;
      i += nold;
    } else {
      i++;
    }
  }
  if (nnew > nold) {
    size_t grow = nnew - nold;
    if (count != 0 && grow > ((size_t)-1) / count)
      sz_panic("sz_string_replace: result too large");
    if (count * grow > ((size_t)-1) - slen)
      sz_panic("sz_string_replace: result too large");
    out_len = slen + count * grow;
  } else {
    out_len = slen - count * (nold - nnew);
  }
  buf = (char *)sz_alloc(out_len + 1);
  i = 0;
  o = 0;
  while (i < slen) {
    if (i + nold <= slen && memcmp(src + i, oldp, nold) == 0) {
      if (nnew) {
        memcpy(buf + o, newp, nnew);
        o += nnew;
      }
      i += nold;
    } else {
      buf[o++] = src[i++];
    }
  }
  buf[out_len] = '\0';
  out = sz_string_from_bytes(buf, out_len);
  sz_free(buf);
  return out;
}

SzList *sz_string_split(const SzString *s, const SzString *sep) {
  size_t slen = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  size_t nsep = sep && sep->data ? sep->len : 0;
  const char *sepp = sep && sep->data ? sep->data : "";
  SzList *acc = NULL;
  size_t i;
  size_t start;
  SzString *part;
  SzList *old;
  SzList *rev;
  if (nsep == 0) {
    SzString *one = sz_string_from_bytes(src, slen);
    acc = sz_list_cons(one, NULL);
    sz_release(one);
    return acc;
  }
  i = 0;
  start = 0;
  while (i + nsep <= slen) {
    if (memcmp(src + i, sepp, nsep) == 0) {
      part = sz_string_from_bytes(src + start, i - start);
      old = acc;
      acc = sz_list_cons(part, old);
      sz_release(part);
      sz_release(old);
      i += nsep;
      start = i;
    } else {
      i++;
    }
  }
  part = sz_string_from_bytes(src + start, slen - start);
  old = acc;
  acc = sz_list_cons(part, old);
  sz_release(part);
  sz_release(old);
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return rev;
}

static int str_is_ws(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

SzString *sz_string_trim(const SzString *s) {
  size_t i;
  size_t j;
  if (!s || !s->data || s->len == 0)
    return sz_string_from_cstr("");
  i = 0;
  j = s->len;
  while (i < j && str_is_ws((unsigned char)s->data[i]))
    i++;
  while (j > i && str_is_ws((unsigned char)s->data[j - 1]))
    j--;
  return sz_string_from_bytes(s->data + i, j - i);
}

int64_t sz_string_is_empty(const SzString *s) {
  return (!s || !s->data || s->len == 0) ? 1 : 0;
}

int64_t sz_string_non_empty(const SzString *s) {
  return sz_string_is_empty(s) ? 0 : 1;
}

static SzString *sz_string_ascii_case(const SzString *s, int to_upper) {
  size_t n = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  char *buf = (char *)sz_alloc(n + 1);
  size_t i;
  SzString *out;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)src[i];
    if (to_upper) {
      if (c >= 'a' && c <= 'z')
        c = (unsigned char)(c - 32);
    } else if (c >= 'A' && c <= 'Z') {
      c = (unsigned char)(c + 32);
    }
    buf[i] = (char)c;
  }
  buf[n] = '\0';
  out = sz_string_from_bytes(buf, n);
  sz_free(buf);
  return out;
}

SzString *sz_string_to_lower(const SzString *s) { return sz_string_ascii_case(s, 0); }

SzString *sz_string_to_upper(const SzString *s) { return sz_string_ascii_case(s, 1); }

SzString *sz_string_capitalize(const SzString *s) {
  size_t n = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  char *buf = (char *)sz_alloc(n + 1);
  size_t i;
  SzString *out;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)src[i];
    if (i == 0 && c >= 'a' && c <= 'z')
      c = (unsigned char)(c - 32);
    buf[i] = (char)c;
  }
  buf[n] = '\0';
  out = sz_string_from_bytes(buf, n);
  sz_free(buf);
  return out;
}

SzString *sz_string_repeat(const SzString *s, int64_t n) {
  size_t slen = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  size_t times;
  size_t out_len;
  char *buf;
  SzString *out;
  size_t i;
  if (n <= 0)
    return sz_string_from_cstr("");
  if (slen == 0)
    return sz_string_from_cstr("");
  if ((uint64_t)n > (uint64_t)SIZE_MAX)
    sz_panic("Str.repeat too large");
  times = (size_t)n;
  if (slen > SIZE_MAX / times)
    sz_panic("Str.repeat too large");
  out_len = slen * times;
  buf = (char *)sz_alloc(out_len + 1);
  for (i = 0; i < times; i++)
    memcpy(buf + i * slen, src, slen);
  buf[out_len] = '\0';
  out = sz_string_from_bytes(buf, out_len);
  sz_free(buf);
  return out;
}

static SzString *sz_string_copy(const SzString *s) {
  size_t n = s && s->data ? s->len : 0;
  const char *src = s && s->data ? s->data : "";
  return sz_string_from_bytes(src, n);
}

SzString *sz_string_strip_prefix(const SzString *s, const SzString *prefix) {
  size_t slen = s && s->data ? s->len : 0;
  size_t plen = prefix && prefix->data ? prefix->len : 0;
  if (!sz_string_starts_with(s, prefix))
    return sz_string_copy(s);
  return sz_string_from_bytes((s && s->data) ? s->data + plen : "", slen - plen);
}

SzString *sz_string_strip_suffix(const SzString *s, const SzString *suffix) {
  size_t slen = s && s->data ? s->len : 0;
  size_t n = suffix && suffix->data ? suffix->len : 0;
  if (!sz_string_ends_with(s, suffix))
    return sz_string_copy(s);
  return sz_string_from_bytes((s && s->data) ? s->data : "", slen - n);
}

static SzString *sz_string_pad(const SzString *s, int64_t n, const SzString *pad, int left) {
  size_t slen = s && s->data ? s->len : 0;
  size_t plen = pad && pad->data ? pad->len : 0;
  const char *src = s && s->data ? s->data : "";
  const char *psrc = pad && pad->data ? pad->data : "";
  size_t want;
  size_t need;
  char *buf;
  SzString *out;
  size_t i;
  if (n <= 0 || (uint64_t)n <= (uint64_t)slen)
    return sz_string_copy(s);
  if (plen == 0)
    return sz_string_copy(s);
  if ((uint64_t)n > (uint64_t)SIZE_MAX)
    sz_panic("Str.pad too large");
  want = (size_t)n;
  need = want - slen;
  buf = (char *)sz_alloc(want + 1);
  if (left) {
    for (i = 0; i < need; i++)
      buf[i] = psrc[i % plen];
    memcpy(buf + need, src, slen);
  } else {
    memcpy(buf, src, slen);
    for (i = 0; i < need; i++)
      buf[slen + i] = psrc[i % plen];
  }
  buf[want] = '\0';
  out = sz_string_from_bytes(buf, want);
  sz_free(buf);
  return out;
}

SzString *sz_string_pad_left(const SzString *s, int64_t n, const SzString *pad) {
  return sz_string_pad(s, n, pad, 1);
}

SzString *sz_string_pad_right(const SzString *s, int64_t n, const SzString *pad) {
  return sz_string_pad(s, n, pad, 0);
}

int64_t sz_string_is_blank(const SzString *s) {
  size_t i;
  if (!s || !s->data || s->len == 0)
    return 1;
  for (i = 0; i < s->len; i++) {
    if (!str_is_ws((unsigned char)s->data[i]))
      return 0;
  }
  return 1;
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
    if (end > start) {
      SzString *line = sz_string_from_bytes(data + start, end - start);
      SzList *old = acc;
      acc = sz_list_cons(line, old);
      sz_release(line);
      sz_release(old);
    }
  }
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    return rev;
  }
}

void *sz_box_i64(int64_t n) {
  int64_t *p = (int64_t *)sz_rc_alloc(sizeof(int64_t), SZ_RC_BOX);
  *p = n;
  return p;
}

int64_t sz_unbox_i64(const void *p) {
  return p ? *(const int64_t *)p : 0;
}

static int list_eq(const SzList *a, const SzList *b) {
  while (a && b) {
    if (!sz_ptr_eq(a->head, b->head))
      return 0;
    a = a->tail;
    b = b->tail;
  }
  return a == NULL && b == NULL;
}

static int map_subset_eq(const SzMap *a, SzMap *b) {
  if (!a)
    return 1;
  if (sz_map_contains(b, a->key) == 0)
    return 0;
  if (!sz_ptr_eq(a->val, sz_map_get_or(b, a->key, NULL)))
    return 0;
  return map_subset_eq(a->left, b) && map_subset_eq(a->right, b);
}

static int map_eq(const SzMap *a, const SzMap *b) {
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  if (a->key_kind != b->key_kind)
    return 0;
  if (sz_map_size((SzMap *)a) != sz_map_size((SzMap *)b))
    return 0;
  return map_subset_eq(a, (SzMap *)b);
}

static int adt_eq(const SzAdt *a, const SzAdt *b) {
  if (a->tag != b->tag)
    return 0;
  return sz_ptr_eq(a->payload, b->payload);
}

static int pair_eq(const SzPair *a, const SzPair *b) {
  return sz_ptr_eq(a->left, b->left) && sz_ptr_eq(a->right, b->right);
}

static int either_eq(const SzEither *a, const SzEither *b) {
  if (a->is_right != b->is_right)
    return 0;
  if (a->is_right)
    return sz_ptr_eq(a->as.right, b->as.right);
  return sz_ptr_eq(a->as.left, b->as.left);
}

static int error_eq(const SzError *a, const SzError *b) {
  if (a->code != b->code)
    return 0;
  return sz_ptr_eq(a->message, b->message);
}

int sz_ptr_eq(const void *a, const void *b) {
  uint32_t ka;
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  if (!sz_is_rc(a) || !sz_is_rc(b))
    return 0;
  ka = sz_rc_hdr(a)->kind;
  if (ka != sz_rc_hdr(b)->kind)
    return 0;
  switch (ka) {
  case SZ_RC_STRING:
    return sz_string_eq((const SzString *)a, (const SzString *)b);
  case SZ_RC_BOX:
    return sz_unbox_i64(a) == sz_unbox_i64(b);
  case SZ_RC_LIST:
    return list_eq((const SzList *)a, (const SzList *)b);
  case SZ_RC_MAP:
    return map_eq((const SzMap *)a, (const SzMap *)b);
  case SZ_RC_ADT:
    return adt_eq((const SzAdt *)a, (const SzAdt *)b);
  case SZ_RC_PAIR:
    return pair_eq((const SzPair *)a, (const SzPair *)b);
  case SZ_RC_EITHER:
    return either_eq((const SzEither *)a, (const SzEither *)b);
  case SZ_RC_ERROR:
    return error_eq((const SzError *)a, (const SzError *)b);
  default:
    return 0;
  }
}

/* --- errors / Either / ADT / Pair ---------------------------------------- */

SzError *sz_error_new(int32_t code, const char *msg) {
  SzError *e = (SzError *)sz_rc_alloc(sizeof(SzError), SZ_RC_ERROR);
  e->code = code;
  e->message = sz_string_from_cstr(msg ? msg : "error");
  return e;
}

void sz_error_free(SzError *err) { sz_release(err); }

SzString *sz_error_message(const SzError *err) {
  if (!err || !err->message)
    return sz_string_from_cstr("error");
  sz_retain(err->message);
  return err->message;
}

SzEither *sz_either_right(void *value) {
  SzEither *e = (SzEither *)sz_rc_alloc(sizeof(SzEither), SZ_RC_EITHER);
  e->is_right = 1;
  sz_retain(value);
  e->as.right = value;
  return e;
}

SzEither *sz_either_left(SzError *err) {
  SzEither *e = (SzEither *)sz_rc_alloc(sizeof(SzEither), SZ_RC_EITHER);
  e->is_right = 0;
  sz_retain(err);
  e->as.left = err;
  return e;
}

void sz_either_free(SzEither *e) { sz_release(e); }

SzAdt *sz_either_to_result(SzEither *e) {
  SzAdt *adt;
  if (e && e->is_right) {
    void *payload = e->as.right;
    e->as.right = NULL;
    adt = sz_adt_new(1, payload);
    sz_release(payload);
  } else {
    const char *msg = "error";
    SzString *s;
    if (e && !e->is_right && e->as.left && e->as.left->message)
      msg = sz_string_cstr(e->as.left->message);
    s = sz_string_from_cstr(msg);
    adt = sz_adt_new(0, s);
    sz_release(s);
  }
  sz_either_free(e);
  return adt;
}

static SzIo *attempt_as_result_cont(void *value, void *env) {
  (void)env;
  return pure_drop(sz_either_to_result((SzEither *)value));
}

SzIo *sz_io_attempt_as_result(SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_attempt_as_result(null)");
  return fm_drop(sz_io_attempt(inner), attempt_as_result_cont, NULL);
}

SzAdt *sz_adt_new(int32_t tag, void *payload) {
  SzAdt *a = (SzAdt *)sz_rc_alloc(sizeof(SzAdt), SZ_RC_ADT);
  a->tag = tag;
  a->payload = payload;
  sz_retain(payload);
  return a;
}

int32_t sz_adt_tag(const SzAdt *adt) { return adt ? adt->tag : -1; }

void *sz_adt_payload(const SzAdt *adt) { return adt ? adt->payload : NULL; }

SzPair *sz_pair_new(void *left, void *right) {
  SzPair *p = (SzPair *)sz_rc_alloc(sizeof(SzPair), SZ_RC_PAIR);
  sz_retain(left);
  sz_retain(right);
  p->left = left;
  p->right = right;
  return p;
}

void sz_pair_free(SzPair *p) { sz_release(p); }

/* --- IO constructors ----------------------------------------------------- */

static SzIo *sz_io_new(SzIoTag tag) {
  SzIo *io = (SzIo *)sz_rc_alloc(sizeof(SzIo), SZ_RC_IO);
  memset(io, 0, sizeof(SzIo));
  io->tag = tag;
  return io;
}

SzIo *sz_io_pure(void *value) {
  SzIo *io = sz_io_new(SZ_IO_PURE);
  sz_retain(value);
  io->as.pure_value = value;
  return io;
}

SzIo *sz_io_delay(SzThunk thunk, void *env) {
  if (!thunk)
    sz_panic("sz_io_delay(null thunk)");
  SzIo *io = sz_io_new(SZ_IO_DELAY);
  io->as.delay.thunk = thunk;
  sz_retain(env);
  io->as.delay.env = env;
  return io;
}

SzIo *sz_io_flatmap(SzIo *inner, SzCont cont, void *env) {
  if (!inner || !cont)
    sz_panic("sz_io_flatmap(null)");
  SzIo *io = sz_io_new(SZ_IO_FLATMAP);
  sz_retain(inner);
  io->as.flatmap.inner = inner;
  io->as.flatmap.cont = cont;
  sz_retain(env);
  io->as.flatmap.env = env;
  return io;
}

SzIo *sz_io_fail(SzError *err) {
  SzIo *io = sz_io_new(SZ_IO_FAIL);
  sz_retain(err);
  io->as.fail = err;
  return io;
}

SzIo *sz_io_fail_cstr(const char *msg) {
  return fail_drop(sz_error_new(1, msg ? msg : "fail"));
}

SzIo *sz_io_println(SzString *msg) {
  if (!msg)
    sz_panic("sz_io_println(null)");
  SzIo *io = sz_io_new(SZ_IO_PRINTLN);
  io->as.println = msg;
  sz_retain(msg);
  return io;
}

SzIo *sz_io_println_cstr(const char *msg) {
  SzString *s = sz_string_from_cstr(msg);
  SzIo *io = sz_io_println(s);
  sz_release(s);
  return io;
}

SzIo *sz_io_handle_error_with(SzIo *inner, SzErrorHandler handler, void *env) {
  if (!inner || !handler)
    sz_panic("sz_io_handle_error_with(null)");
  SzIo *io = sz_io_new(SZ_IO_HANDLE_ERROR);
  sz_retain(inner);
  io->as.handle_error.inner = inner;
  io->as.handle_error.handler = handler;
  sz_retain(env);
  io->as.handle_error.env = env;
  return io;
}

SzIo *sz_io_attempt(SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_attempt(null)");
  SzIo *io = sz_io_new(SZ_IO_ATTEMPT);
  sz_retain(inner);
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
  sz_retain(left);
  sz_retain(right);
  io->as.race.left = left;
  io->as.race.right = right;
  return io;
}

SzIo *sz_io_both(SzIo *left, SzIo *right) {
  if (!left || !right)
    sz_panic("sz_io_both(null)");
  SzIo *io = sz_io_new(SZ_IO_BOTH);
  sz_retain(left);
  sz_retain(right);
  io->as.both.left = left;
  io->as.both.right = right;
  return io;
}

SzIo *sz_io_ensure(SzIo *inner, SzIo *finalizer) {
  if (!inner || !finalizer)
    sz_panic("sz_io_ensure(null)");
  SzIo *io = sz_io_new(SZ_IO_ENSURE);
  sz_retain(inner);
  sz_retain(finalizer);
  io->as.ensure.inner = inner;
  io->as.ensure.finalizer = finalizer;
  return io;
}

SzIo *sz_io_timeout(int64_t ms, SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_timeout(null)");
  SzIo *io = sz_io_new(SZ_IO_TIMEOUT);
  io->as.timeout.ms = ms < 0 ? 0 : ms;
  sz_retain(inner);
  io->as.timeout.inner = inner;
  return io;
}

SzIo *sz_io_forever(SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_forever(null)");
  SzIo *io = sz_io_new(SZ_IO_FOREVER);
  sz_retain(inner);
  io->as.loop.inner = inner;
  io->as.loop.n = 0;
  return io;
}

SzIo *sz_io_repeat_n(int64_t n, SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_repeat_n(null)");
  SzIo *io = sz_io_new(SZ_IO_REPEAT_N);
  sz_retain(inner);
  io->as.loop.inner = inner;
  io->as.loop.n = n < 0 ? 0 : n;
  return io;
}

SzIo *sz_io_retry_n(int64_t n, SzIo *inner) {
  if (!inner)
    sz_panic("sz_io_retry_n(null)");
  SzIo *io = sz_io_new(SZ_IO_RETRY_N);
  sz_retain(inner);
  io->as.loop.inner = inner;
  io->as.loop.n = n < 0 ? 0 : n;
  return io;
}

SzIo *sz_fiber_fork(SzIo *inner) {
  if (!inner)
    sz_panic("sz_fiber_fork(null)");
  SzIo *io = sz_io_new(SZ_IO_FORK);
  sz_retain(inner);
  io->as.fork_inner = inner;
  return io;
}

SzIo *sz_fiber_join(void *fiber) {
  if (!fiber)
    return sz_io_fail_cstr("Fiber.join: null fiber");
  SzIo *io = sz_io_new(SZ_IO_JOIN);
  io->as.fiber = fiber;
  return io;
}

SzIo *sz_fiber_interrupt(void *fiber) {
  if (!fiber)
    return sz_io_fail_cstr("Fiber.interrupt: null fiber");
  SzIo *io = sz_io_new(SZ_IO_INTERRUPT);
  io->as.fiber = fiber;
  return io;
}

SzIo *sz_io_queue_take(SzQueue *q) {
  SzIo *io = sz_io_new(SZ_IO_QUEUE_TAKE);
  sz_retain(q);
  io->as.queue_take = q;
  return io;
}

SzIo *sz_io_deferred_get(SzDeferred *d) {
  SzIo *io = sz_io_new(SZ_IO_DEFERRED_GET);
  sz_retain(d);
  io->as.deferred_get = d;
  return io;
}

static SzIo *sz_io_poll_fd(int fd, int events) {
  SzIo *io;
  if (fd < 0)
    return sz_io_fail_cstr("poll: invalid fd");
  io = sz_io_new(SZ_IO_POLL_FD);
  io->as.poll.fd = fd;
  io->as.poll.events = events ? events : POLLIN;
  return io;
}

SzIo *sz_io_poll_readable(int fd) { return sz_io_poll_fd(fd, POLLIN); }

SzIo *sz_io_poll_writable(int fd) { return sz_io_poll_fd(fd, POLLOUT); }

/* --- cooperative fiber scheduler (single-threaded, deterministic) -------- */

typedef enum ContKind {
  CONT_FLATMAP = 1,
  CONT_HANDLE = 2,
  CONT_ENSURE = 3,
  CONT_LOOP = 4
} ContKind;

typedef enum LoopKind {
  LOOP_NONE = 0,
  LOOP_FOREVER = 1,
  LOOP_REPEAT = 2,
  LOOP_RETRY = 3
} LoopKind;

typedef struct ContFrame {
  ContKind kind;
  SzCont cont;
  SzErrorHandler handler;
  SzIo *finalizer;
  void *env;
  LoopKind loop_kind;
  SzIo *loop_inner;
  int64_t loop_left;
  struct ContFrame *next;
} ContFrame;

typedef enum FiberState {
  FIB_READY = 1,
  FIB_SLEEP,
  FIB_QWAIT,
  FIB_DWAIT,
  FIB_POLL,
  FIB_JOIN,
  FIB_FWAIT,
  FIB_DONE,
  FIB_CANCELLED,
  FIB_FINALIZING
} FiberState;

typedef enum JoinKind {
  JOIN_NONE = 0,
  JOIN_RACE = 1,
  JOIN_BOTH = 2,
  JOIN_TIMEOUT = 3
} JoinKind;

typedef struct Fiber {
  ContFrame *stack;
  SzIo *cur;
  FiberState state;
  int64_t wake_at;
  int poll_fd;
  int poll_events;
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
  int forked; /* 1 if created by Fiber.fork */
  void *join_waiters; /* Fiber* list parked on Fiber.join / interrupt */
  struct Fiber *fwait; /* target when state is FIB_FWAIT */
  int fwait_join; /* 1 join (value/error), 0 interrupt (Unit on settle) */
  struct Fiber *forked_next;
  struct Fiber *ready_next;
  struct Fiber *wait_next;
  struct Fiber *all_next;
} Fiber;

typedef struct Sched {
  Fiber *ready_head;
  Fiber *ready_tail;
  Fiber *sleepers;
  Fiber *pollers;
  Fiber *root;
  Fiber *current;
  Fiber *forked_live;
  Fiber *all_fibers;
  int sched_armed;   /* 1 when SCUZZ_SCHED_SEED is set */
  int32_t sched_rng; /* Lehmer/MINSTD state in 1..2147483646 */
} Sched;

static Sched *g_sched = NULL;

/* Same LCG as `scuzz fuzz` schedule search (`crates/compiler/src/fuzz.rs`). */
enum { SZ_LCG_M = 2147483647, SZ_LCG_A = 48271, SZ_LCG_SEED_MOD = 2147483646 };

static int32_t sched_lcg_seed(int32_t seed) {
  int32_t m = seed % SZ_LCG_SEED_MOD;
  if (m < 0)
    m += SZ_LCG_SEED_MOD;
  return m + 1;
}

static int32_t sched_lcg_next(int32_t s) {
  return (int32_t)(((int64_t)s * (int64_t)SZ_LCG_A) % (int64_t)SZ_LCG_M);
}

static void sched_arm_from_env(Sched *s) {
  const char *env = getenv("SCUZZ_SCHED_SEED");
  s->sched_armed = 0;
  s->sched_rng = 1;
  if (!env)
    return;
  /* Present (including "0") arms seed-driven pick; absent keeps FIFO. */
  s->sched_armed = 1;
  s->sched_rng = sched_lcg_seed((int32_t)atoi(env));
}

static ContFrame *cont_push_flatmap(ContFrame *stack, SzCont cont, void *env) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_FLATMAP;
  f->cont = cont;
  f->handler = NULL;
  f->finalizer = NULL;
  f->env = env;
  f->loop_kind = LOOP_NONE;
  f->loop_inner = NULL;
  f->loop_left = 0;
  f->next = stack;
  return f;
}

static ContFrame *cont_push_handle(ContFrame *stack, SzErrorHandler handler,
                                   void *env) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_HANDLE;
  f->cont = NULL;
  f->handler = handler;
  f->finalizer = NULL;
  f->env = env;
  f->loop_kind = LOOP_NONE;
  f->loop_inner = NULL;
  f->loop_left = 0;
  f->next = stack;
  return f;
}

static ContFrame *cont_push_ensure(ContFrame *stack, SzIo *finalizer) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_ENSURE;
  f->cont = NULL;
  f->handler = NULL;
  f->finalizer = finalizer;
  f->env = NULL;
  f->loop_kind = LOOP_NONE;
  f->loop_inner = NULL;
  f->loop_left = 0;
  f->next = stack;
  return f;
}

static ContFrame *cont_push_loop(ContFrame *stack, LoopKind lk, SzIo *inner,
                                int64_t left) {
  ContFrame *f = (ContFrame *)sz_alloc(sizeof(ContFrame));
  f->kind = CONT_LOOP;
  f->cont = NULL;
  f->handler = NULL;
  f->finalizer = NULL;
  f->env = NULL;
  f->loop_kind = lk;
  f->loop_inner = inner;
  f->loop_left = left < 0 ? 0 : left;
  f->next = stack;
  return f;
}

static ContFrame *cont_pop(ContFrame *stack) {
  ContFrame *next = stack->next;
  if (stack->kind == CONT_LOOP)
    sz_release(stack->loop_inner);
  if (stack->kind == CONT_ENSURE)
    sz_release(stack->finalizer);
  if (stack->kind == CONT_FLATMAP || stack->kind == CONT_HANDLE)
    sz_release(stack->env);
  sz_free(stack);
  return next;
}

static void cont_free_all(ContFrame *stack) {
  while (stack)
    stack = cont_pop(stack);
}

/* Attempt success continuation: wrap value as Right. */
static SzIo *attempt_ok(void *value, void *env) {
  (void)env;
  return pure_drop(either_right_drop(value));
}

static SzIo *attempt_err(SzError *err, void *env) {
  (void)env;
  return pure_drop(either_left_drop(err));
}

typedef struct EnsureExit {
  void *value;
  SzError *err;
} EnsureExit;

static SzIo *ensure_after_attempt_ok(void *either_val, void *env) {
  EnsureExit *e = (EnsureExit *)env;
  void *v = e->value;
  SzEither *ex = (SzEither *)either_val;
  sz_free(e);
  if (ex && ex->is_right) {
    sz_either_free(ex);
    return pure_drop(v);
  }
  {
    SzError *err;
    if (ex && !ex->is_right && ex->as.left) {
      err = ex->as.left;
      ex->as.left = NULL;
    } else {
      err = sz_error_new(2, "ensure finalizer failed");
    }
    if (ex)
      sz_either_free(ex);
    sz_release(v);
    return fail_drop(err);
  }
}

static SzIo *ensure_after_attempt_err(void *either_val, void *env) {
  EnsureExit *e = (EnsureExit *)env;
  SzError *orig = e->err;
  SzEither *ex = (SzEither *)either_val;
  sz_free(e);
  if (ex && !ex->is_right && ex->as.left && ex->as.left != orig)
    sz_error_free(ex->as.left);
  if (ex) {
    if (!ex->is_right)
      ex->as.left = NULL;
    sz_either_free(ex);
  }
  return fail_drop(orig);
}

static SzIo *ensure_run_fin_ok(SzIo *fin, void *value) {
  EnsureExit *e = (EnsureExit *)sz_alloc(sizeof(EnsureExit));
  e->value = value;
  e->err = NULL;
  return fm_drop(attempt_drop(fin), ensure_after_attempt_ok, e);
}

static SzIo *ensure_run_fin_err(SzIo *fin, SzError *err) {
  EnsureExit *e = (EnsureExit *)sz_alloc(sizeof(EnsureExit));
  e->value = NULL;
  e->err = err;
  return fm_drop(attempt_drop(fin), ensure_after_attempt_err, e);
}

static SzIo *ignore_then_io(void *ignored, void *env) {
  (void)ignored;
  return (SzIo *)env;
}

/* Sequence finalizers LIFO (top of stack first). Takes ownership of ENSURE
 * frames' finalizer pointers. Releases flatMap/handle env like cont_pop.
 * Frees the whole cont stack. */
static SzIo *drain_ensure_finalizers(ContFrame *stack) {
  SzIo *acc = NULL;
  ContFrame *c = stack;
  while (c) {
    ContFrame *next = c->next;
    if (c->kind == CONT_ENSURE && c->finalizer) {
      SzIo *fin = c->finalizer;
      c->finalizer = NULL;
      if (!acc)
        acc = fin;
      else
        acc = fm_drop(acc, ignore_then_io, fin);
    } else if (c->kind == CONT_LOOP)
      sz_release(c->loop_inner);
    else if (c->kind == CONT_FLATMAP || c->kind == CONT_HANDLE)
      sz_release(c->env);
    sz_free(c);
    c = next;
  }
  return acc;
}

/* Steal unstepped ENSURE finalizers from cur (inner-most first). Nested
 * ENSURE nodes fold the same way. */
static SzIo *take_unstepped_ensure(SzIo *cur) {
  SzIo *inner;
  SzIo *fin;
  SzIo *acc;
  if (!cur || cur->tag != SZ_IO_ENSURE)
    return NULL;
  inner = cur->as.ensure.inner;
  fin = cur->as.ensure.finalizer;
  if (sz_is_rc(cur) && sz_rc_hdr(cur)->rc > 1) {
    if (fin)
      sz_retain(fin);
  } else {
    cur->as.ensure.finalizer = NULL;
  }
  acc = take_unstepped_ensure(inner);
  if (!acc)
    return fin;
  if (!fin)
    return acc;
  return fm_drop(acc, ignore_then_io, fin);
}

static void ready_enqueue(Sched *s, Fiber *f) {
  if (!f || f->state == FIB_CANCELLED || f->state == FIB_DONE)
    return;
  /* Keep FIB_FINALIZING so cancel cleanup does not join_child_done as success. */
  if (f->state != FIB_FINALIZING)
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

static int ready_count(Sched *s) {
  int n = 0;
  Fiber *f = s->ready_head;
  while (f) {
    n++;
    f = f->ready_next;
  }
  return n;
}

/* FIFO unlink of the head. */
static Fiber *ready_dequeue_fifo(Sched *s) {
  Fiber *f = s->ready_head;
  if (!f)
    return NULL;
  s->ready_head = f->ready_next;
  if (!s->ready_head)
    s->ready_tail = NULL;
  f->ready_next = NULL;
  return f;
}

/* Unlink the k-th ready fiber (0-based). */
static Fiber *ready_unlink_at(Sched *s, int k) {
  Fiber *prev = NULL;
  Fiber *f = s->ready_head;
  int i;
  if (k <= 0)
    return ready_dequeue_fifo(s);
  for (i = 0; f && i < k; i++) {
    prev = f;
    f = f->ready_next;
  }
  if (!f)
    return ready_dequeue_fifo(s);
  prev->ready_next = f->ready_next;
  if (!f->ready_next)
    s->ready_tail = prev;
  f->ready_next = NULL;
  return f;
}

/* Pick next ready fiber: FIFO when disarmed or n<=1; else seed-driven among n. */
static Fiber *ready_dequeue(Sched *s) {
  int n;
  int k;
  if (!s->ready_head)
    return NULL;
  if (!s->sched_armed)
    return ready_dequeue_fifo(s);
  n = ready_count(s);
  if (n <= 1)
    return ready_dequeue_fifo(s);
  k = (int)(s->sched_rng % n);
  s->sched_rng = sched_lcg_next(s->sched_rng);
  return ready_unlink_at(s, k);
}

static void ready_remove(Sched *s, Fiber *f) {
  Fiber **pp;
  Fiber *prev;
  if (!f)
    return;
  pp = &s->ready_head;
  prev = NULL;
  while (*pp) {
    if (*pp == f) {
      *pp = f->ready_next;
      if (s->ready_tail == f)
        s->ready_tail = prev;
      f->ready_next = NULL;
      return;
    }
    prev = *pp;
    pp = &(*pp)->ready_next;
  }
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
  Fiber **pp = (Fiber **)&q->waiters;
  f->wait_next = NULL;
  while (*pp)
    pp = &(*pp)->wait_next;
  *pp = f;
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

static void poller_add(Sched *s, Fiber *f) {
  f->wait_next = s->pollers;
  s->pollers = f;
}

static void poller_remove(Sched *s, Fiber *f) {
  Fiber **pp = &s->pollers;
  while (*pp) {
    if (*pp == f) {
      *pp = f->wait_next;
      f->wait_next = NULL;
      return;
    }
    pp = &(*pp)->wait_next;
  }
}

static void forked_live_add(Sched *s, Fiber *f) {
  f->forked_next = s->forked_live;
  s->forked_live = f;
}

static void forked_live_remove(Sched *s, Fiber *f) {
  Fiber **pp = &s->forked_live;
  while (*pp) {
    if (*pp == f) {
      *pp = f->forked_next;
      f->forked_next = NULL;
      return;
    }
    pp = &(*pp)->forked_next;
  }
}

static void fiber_join_waiter_add(Fiber *target, Fiber *w) {
  w->wait_next = (Fiber *)target->join_waiters;
  target->join_waiters = w;
}

static void fiber_join_waiter_remove(Fiber *target, Fiber *w) {
  Fiber **pp;
  if (!target)
    return;
  pp = (Fiber **)&target->join_waiters;
  while (*pp) {
    if (*pp == w) {
      *pp = w->wait_next;
      w->wait_next = NULL;
      return;
    }
    pp = &(*pp)->wait_next;
  }
}

static SzError *fiber_interrupt_err(void) {
  return sz_error_new(9, "fiber interrupted");
}

static SzError *error_copy_or_interrupt(SzError *err) {
  if (!err)
    return fiber_interrupt_err();
  return sz_error_new(err->code, sz_string_cstr(err->message));
}

static SzError *deferred_copy_error(SzDeferred *d) {
  return d->error ? error_copy_or_interrupt(d->error)
                  : sz_error_new(1, "deferred failed");
}

/* Unique parent: take the child. Shared parent (loop template): retain. */
static SzIo *io_child(SzIo *parent, SzIo **slot) {
  SzIo *c = *slot;
  if (!c)
    return NULL;
  if (parent && sz_is_rc(parent) && sz_rc_hdr(parent)->rc > 1) {
    sz_retain(c);
    return c;
  }
  *slot = NULL;
  return c;
}

static void *io_env_child(SzIo *parent, void **slot) {
  void *e = *slot;
  if (!e)
    return NULL;
  if (parent && sz_is_rc(parent) && sz_rc_hdr(parent)->rc > 1) {
    sz_retain(e);
    return e;
  }
  *slot = NULL;
  return e;
}

static void fiber_set_cur(Fiber *f, SzIo *next) {
  SzIo *old = f->cur;
  f->cur = next;
  if (old != next)
    sz_release(old);
}

/* Retain so the run result does not alias a live slot. */
static void fiber_set_pure_retained(Fiber *f, void *value) {
  sz_retain(value);
  fiber_set_cur(f, pure_drop(value));
}

static Fiber *fiber_new(SzIo *cur, Fiber *parent, JoinKind jk, int slot) {
  Fiber *f = (Fiber *)sz_alloc_zero(sizeof(Fiber));
  f->cur = cur;
  f->state = FIB_READY;
  f->parent = parent;
  f->join_kind = jk;
  f->child_slot = slot;
  if (g_sched) {
    f->all_next = g_sched->all_fibers;
    g_sched->all_fibers = f;
  }
  return f;
}

static void fiber_resume_value(Sched *s, Fiber *f, void *value);
static void fiber_fail(Sched *s, Fiber *f, SzError *err);
static void fiber_wake_joiners(Sched *s, Fiber *target, int ok, void *val,
                               SzError *err);
static void fiber_settle_cancelled(Sched *s, Fiber *f);

static void fiber_cancel(Sched *s, Fiber *f) {
  SzIo *cleanup;
  Fiber *nested;
  if (!f || f->state == FIB_DONE || f->state == FIB_CANCELLED ||
      f->state == FIB_FINALIZING)
    return;
  ready_remove(s, f);
  if (f->state == FIB_SLEEP)
    sleeper_remove(s, f);
  if (f->state == FIB_QWAIT && f->qwait) {
    queue_waiter_remove(f->qwait, f);
    sz_release(f->qwait);
    f->qwait = NULL;
  }
  if (f->state == FIB_DWAIT && f->dwait) {
    def_waiter_remove(f->dwait, f);
    sz_release(f->dwait);
    f->dwait = NULL;
  }
  if (f->state == FIB_POLL)
    poller_remove(s, f);
  if (f->state == FIB_FWAIT && f->fwait)
    fiber_join_waiter_remove(f->fwait, f);
  if (f->children[0])
    fiber_cancel(s, f->children[0]);
  if (f->children[1])
    fiber_cancel(s, f->children[1]);
  nested = s->forked_live;
  while (nested) {
    Fiber *next = nested->forked_next;
    if (nested->parent == f)
      fiber_cancel(s, nested);
    nested = next;
  }
  {
    SzIo *from_cur = take_unstepped_ensure(f->cur);
    SzIo *from_stack = drain_ensure_finalizers(f->stack);
    f->stack = NULL;
    if (from_cur && from_stack)
      cleanup = fm_drop(from_cur, ignore_then_io, from_stack);
    else
      cleanup = from_cur ? from_cur : from_stack;
  }
  fiber_set_cur(f, cleanup);
  if (cleanup) {
    f->state = FIB_FINALIZING;
    ready_enqueue(s, f);
  } else {
    fiber_settle_cancelled(s, f);
  }
}

static void fiber_wake_joiners(Sched *s, Fiber *target, int ok, void *val,
                               SzError *err) {
  Fiber *w;
  if (!target)
    return;
  w = (Fiber *)target->join_waiters;
  target->join_waiters = NULL;
  while (w) {
    Fiber *next = w->wait_next;
    int want_join = w->fwait_join;
    w->wait_next = NULL;
    w->fwait = NULL;
    if (w->state == FIB_FWAIT) {
      if (!want_join) {
        fiber_set_cur(w, pure_drop(NULL));
        ready_enqueue(s, w);
      } else if (ok) {
        fiber_set_pure_retained(w, val);
        ready_enqueue(s, w);
      } else {
        fiber_fail(s, w, error_copy_or_interrupt(err));
      }
    }
    w = next;
  }
}

static void fiber_settle_cancelled(Sched *s, Fiber *f) {
  f->state = FIB_CANCELLED;
  f->result_ok = 0;
  if (!f->result_error)
    f->result_error = fiber_interrupt_err();
  if (f->forked)
    forked_live_remove(s, f);
  fiber_wake_joiners(s, f, 0, NULL, f->result_error);
}

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
      fiber_set_pure_retained(p, val);
      p->join_kind = JOIN_NONE;
      ready_enqueue(s, p);
      return;
    }
    if (p->children_settled >= 2) {
      SzError *e0 = p->child_err[0];
      SzError *e1 = p->child_err[1];
      SzError *out = e1 ? e1 : e0;
      p->join_kind = JOIN_NONE;
      if (out)
        fiber_fail(s, p, error_copy_or_interrupt(out));
      else
        fiber_fail(s, p, sz_error_new(6, "race both failed"));
    }
    return;
  }

  if (p->join_kind == JOIN_TIMEOUT) {
    Fiber *sib = p->children[1 - slot];
    if (sib)
      fiber_cancel(s, sib);
    p->join_kind = JOIN_NONE;
    if (slot == 1) {
      if (ok) {
        p->state = FIB_READY;
        fiber_set_pure_retained(p, val);
        ready_enqueue(s, p);
      } else {
        fiber_fail(s, p, err ? error_copy_or_interrupt(err)
                             : sz_error_new(1, "timeout inner failed"));
      }
    } else {
      fiber_fail(s, p, sz_error_new(1, "timeout"));
    }
    return;
  }

  if (p->join_kind == JOIN_BOTH) {
    if (!ok) {
      Fiber *sib = p->children[1 - slot];
      if (sib)
        fiber_cancel(s, sib);
      p->join_kind = JOIN_NONE;
      fiber_fail(s, p, err ? error_copy_or_interrupt(err)
                           : sz_error_new(7, "both failed"));
      return;
    }
    if (p->children_settled >= 2) {
      p->join_kind = JOIN_NONE;
      p->state = FIB_READY;
      fiber_set_cur(p, pure_drop(sz_pair_new(p->child_val[0], p->child_val[1])));
      ready_enqueue(s, p);
    }
  }
}

static void fiber_finish(Sched *s, Fiber *f, int ok, void *val, SzError *err) {
  if (f->state == FIB_CANCELLED)
    return;
  if (f->state == FIB_FINALIZING) {
    cont_free_all(f->stack);
    f->stack = NULL;
    fiber_set_cur(f, NULL);
    if (err)
      sz_error_free(err);
    fiber_settle_cancelled(s, f);
    return;
  }
  cont_free_all(f->stack);
  f->stack = NULL;
  fiber_set_cur(f, NULL);
  f->state = FIB_DONE;
  f->result_ok = ok;
  f->result_value = val;
  f->result_error = err;
  if (f->forked)
    forked_live_remove(s, f);
  fiber_wake_joiners(s, f, ok, val, err);
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
  if (stack->kind == CONT_LOOP) {
    if (stack->loop_kind == LOOP_FOREVER ||
        (stack->loop_kind == LOOP_REPEAT && stack->loop_left > 0)) {
      if (stack->loop_kind == LOOP_REPEAT)
        stack->loop_left--;
      sz_release(value);
      if (f->cur != stack->loop_inner)
        sz_retain(stack->loop_inner);
      fiber_set_cur(f, stack->loop_inner);
      if (!f->cur) {
        fiber_finish(s, f, 0, NULL, sz_error_new(5, "loop inner is null"));
        return;
      }
      ready_enqueue(s, f);
      return;
    }
    f->stack = cont_pop(stack);
    fiber_resume_value(s, f, value);
    return;
  }
  if (stack->kind == CONT_ENSURE) {
    SzIo *fin = stack->finalizer;
    stack->finalizer = NULL;
    f->stack = cont_pop(stack);
    fiber_set_cur(f, ensure_run_fin_ok(fin, value));
    ready_enqueue(s, f);
    return;
  }
  {
    SzCont cont = stack->cont;
    void *env = stack->env;
    SzIo *next;
    int env_rc = sz_is_rc(env);
    stack->env = NULL;
    f->stack = cont_pop(stack);
    next = cont(value, env);
    if (env_rc)
      sz_release(env);
    fiber_set_cur(f, next);
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
      SzIo *next;
      int env_rc = sz_is_rc(env);
      stack->env = NULL;
      f->stack = cont_pop(stack);
      next = handler(err, env);
      if (env_rc)
        sz_release(env);
      fiber_set_cur(f, next);
      if (!f->cur) {
        fiber_finish(s, f, 0, NULL,
                     sz_error_new(2, "error handler returned null"));
        return;
      }
      ready_enqueue(s, f);
      return;
    }
    if (stack->kind == CONT_ENSURE) {
      SzIo *fin = stack->finalizer;
      stack->finalizer = NULL;
      f->stack = cont_pop(stack);
      fiber_set_cur(f, ensure_run_fin_err(fin, err));
      ready_enqueue(s, f);
      return;
    }
    if (stack->kind == CONT_LOOP && stack->loop_kind == LOOP_RETRY &&
        stack->loop_left > 0) {
      stack->loop_left--;
      if (err)
        sz_error_free(err);
      if (f->cur != stack->loop_inner)
        sz_retain(stack->loop_inner);
      fiber_set_cur(f, stack->loop_inner);
      if (!f->cur) {
        fiber_finish(s, f, 0, NULL, sz_error_new(5, "retry inner is null"));
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
  /* Transfer the offer retain. Do not retain again. */
  fiber_set_cur(f, pure_drop(value));
  ready_enqueue(s, f);
  sz_release(q);
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
    if (f->state == FIB_CANCELLED) {
      sz_release(d);
      continue;
    }
    f->state = FIB_READY;
    if (!d->ok)
      fiber_set_cur(f, fail_drop(deferred_copy_error(d)));
    else
      fiber_set_pure_retained(f, d->value);
    ready_enqueue(s, f);
    sz_release(d);
  }
}

static void park_sleep(Sched *s, Fiber *f, int64_t ms) {
  int64_t now = sz_clock_monotonic_ms_sync();
  f->wake_at = now + (ms < 0 ? 0 : ms);
  f->state = FIB_SLEEP;
  fiber_set_cur(f, NULL);
  sleeper_add(s, f);
}

static int step_fiber(Sched *s, Fiber *f) {
  SzIo *cur;
  if (f->state == FIB_CANCELLED || f->state == FIB_DONE || f->state == FIB_JOIN ||
      f->state == FIB_FWAIT)
    return 0;
  cur = f->cur;
  if (!cur) {
    fiber_finish(s, f, 0, NULL, sz_error_new(1, "null IO"));
    return 0;
  }
  s->current = f;

  switch (cur->tag) {
  case SZ_IO_PURE:
    sz_retain(cur->as.pure_value);
    fiber_resume_value(s, f, cur->as.pure_value);
    return 0;
  case SZ_IO_DELAY: {
    void *env = cur->as.delay.env;
    int steal = 0;
    /* Thunks borrow env. Unique delay (not BOX) steals so the runner drops
     * env after the thunk. Shared delay leaves env so a loop can rerun.
     * BOX packs stay on the node so last-use drops them. */
    if (!(sz_is_rc(cur) && sz_rc_hdr(cur)->rc > 1) &&
        !(sz_is_rc(env) && sz_rc_hdr(env)->kind == SZ_RC_BOX)) {
      cur->as.delay.env = NULL;
      steal = 1;
    }
    {
      void *value = cur->as.delay.thunk(env);
      if (steal)
        delay_env_drop(env);
      fiber_set_cur(f, pure_drop(value));
    }
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
    fiber_set_cur(f, pure_drop(NULL));
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_SLEEP_MS:
    park_sleep(s, f, cur->as.sleep_ms);
    return 0;
  case SZ_IO_FAIL: {
    SzError *err = cur->as.fail;
    if (err)
      sz_retain(err);
    else
      err = sz_error_new(3, "unknown failure");
    fiber_fail(s, f, err);
    return 0;
  }
  case SZ_IO_FLATMAP: {
    SzIo *inner = io_child(cur, &cur->as.flatmap.inner);
    f->stack =
        cont_push_flatmap(f->stack, cur->as.flatmap.cont,
                          io_env_child(cur, &cur->as.flatmap.env));
    fiber_set_cur(f, inner);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL, sz_error_new(5, "flatMap inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_HANDLE_ERROR: {
    SzIo *inner = io_child(cur, &cur->as.handle_error.inner);
    f->stack = cont_push_handle(f->stack, cur->as.handle_error.handler,
                                io_env_child(cur, &cur->as.handle_error.env));
    fiber_set_cur(f, inner);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL,
                   sz_error_new(5, "handleErrorWith inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_ATTEMPT: {
    SzIo *inner = io_child(cur, &cur->as.attempt_inner);
    SzIo *mapped = fm_drop(inner, attempt_ok, NULL);
    SzIo *handled = sz_io_handle_error_with(mapped, attempt_err, NULL);
    sz_release(mapped);
    fiber_set_cur(f, handled);
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_RACE: {
    Fiber *left = fiber_new(io_child(cur, &cur->as.race.left), f, JOIN_RACE, 0);
    Fiber *right = fiber_new(io_child(cur, &cur->as.race.right), f, JOIN_RACE, 1);
    f->children[0] = left;
    f->children[1] = right;
    f->children_settled = 0;
    f->join_kind = JOIN_RACE;
    f->state = FIB_JOIN;
    fiber_set_cur(f, NULL);
    ready_enqueue(s, left);
    ready_enqueue(s, right);
    return 0;
  }
  case SZ_IO_BOTH: {
    Fiber *left = fiber_new(io_child(cur, &cur->as.both.left), f, JOIN_BOTH, 0);
    Fiber *right = fiber_new(io_child(cur, &cur->as.both.right), f, JOIN_BOTH, 1);
    f->children[0] = left;
    f->children[1] = right;
    f->children_settled = 0;
    f->join_kind = JOIN_BOTH;
    f->state = FIB_JOIN;
    fiber_set_cur(f, NULL);
    ready_enqueue(s, left);
    ready_enqueue(s, right);
    return 0;
  }
  case SZ_IO_ENSURE: {
    SzIo *fin = io_child(cur, &cur->as.ensure.finalizer);
    SzIo *inner = io_child(cur, &cur->as.ensure.inner);
    f->stack = cont_push_ensure(f->stack, fin);
    fiber_set_cur(f, inner);
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_TIMEOUT: {
    SzIo *body_io = io_child(cur, &cur->as.timeout.inner);
    Fiber *timer = fiber_new(sz_io_sleep_ms(cur->as.timeout.ms), f, JOIN_TIMEOUT, 0);
    Fiber *body = fiber_new(body_io, f, JOIN_TIMEOUT, 1);
    f->children[0] = timer;
    f->children[1] = body;
    f->children_settled = 0;
    f->join_kind = JOIN_TIMEOUT;
    f->state = FIB_JOIN;
    fiber_set_cur(f, NULL);
    ready_enqueue(s, timer);
    ready_enqueue(s, body);
    return 0;
  }
  case SZ_IO_FOREVER: {
    SzIo *inner = cur->as.loop.inner;
    sz_retain(inner);
    f->stack = cont_push_loop(f->stack, LOOP_FOREVER, inner, 0);
    cur->as.loop.inner = NULL;
    fiber_set_cur(f, inner);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL, sz_error_new(5, "forever inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_REPEAT_N: {
    SzIo *inner = cur->as.loop.inner;
    sz_retain(inner);
    f->stack =
        cont_push_loop(f->stack, LOOP_REPEAT, inner, cur->as.loop.n);
    cur->as.loop.inner = NULL;
    fiber_set_cur(f, inner);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL, sz_error_new(5, "repeatN inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_RETRY_N: {
    SzIo *inner = cur->as.loop.inner;
    sz_retain(inner);
    f->stack =
        cont_push_loop(f->stack, LOOP_RETRY, inner, cur->as.loop.n);
    cur->as.loop.inner = NULL;
    fiber_set_cur(f, inner);
    if (!f->cur) {
      fiber_finish(s, f, 0, NULL, sz_error_new(5, "retryN inner is null"));
      return 0;
    }
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_FORK: {
    SzIo *inner = io_child(cur, &cur->as.fork_inner);
    Fiber *child = fiber_new(inner, f, JOIN_NONE, 0);
    child->forked = 1;
    forked_live_add(s, child);
    ready_enqueue(s, child);
    fiber_set_cur(f, pure_drop((void *)child));
    ready_enqueue(s, f);
    return 0;
  }
  case SZ_IO_JOIN: {
    Fiber *target = (Fiber *)cur->as.fiber;
    if (!target) {
      fiber_fail(s, f, sz_error_new(1, "null fiber"));
      return 0;
    }
    if (target->state == FIB_DONE) {
      if (target->result_ok) {
        fiber_set_pure_retained(f, target->result_value);
        ready_enqueue(s, f);
      } else {
        fiber_fail(s, f, error_copy_or_interrupt(target->result_error));
      }
      return 0;
    }
    if (target->state == FIB_CANCELLED) {
      fiber_fail(s, f, error_copy_or_interrupt(target->result_error));
      return 0;
    }
    f->state = FIB_FWAIT;
    f->fwait = target;
    f->fwait_join = 1;
    fiber_set_cur(f, NULL);
    fiber_join_waiter_add(target, f);
    return 0;
  }
  case SZ_IO_INTERRUPT: {
    Fiber *target = (Fiber *)cur->as.fiber;
    if (!target) {
      fiber_fail(s, f, sz_error_new(1, "null fiber"));
      return 0;
    }
    if (target->state == FIB_DONE || target->state == FIB_CANCELLED) {
      fiber_set_cur(f, pure_drop(NULL));
      ready_enqueue(s, f);
      return 0;
    }
    fiber_cancel(s, target);
    if (target->state == FIB_DONE || target->state == FIB_CANCELLED) {
      fiber_set_cur(f, pure_drop(NULL));
      ready_enqueue(s, f);
      return 0;
    }
    f->state = FIB_FWAIT;
    f->fwait = target;
    f->fwait_join = 0;
    fiber_set_cur(f, NULL);
    fiber_join_waiter_add(target, f);
    return 0;
  }
  case SZ_IO_QUEUE_TAKE: {
    SzQueue *q = cur->as.queue_take;
    cur->as.queue_take = NULL;
    if (!q) {
      fiber_finish(s, f, 0, NULL, sz_error_new(1, "null queue"));
      return 0;
    }
    if (q->len > 0) {
      void *v = q->items[q->head];
      q->items[q->head] = NULL;
      q->head++;
      if (q->head == q->cap)
        q->head = 0;
      q->len--;
      /* Transfer the offer retain. Do not retain again. */
      fiber_set_cur(f, pure_drop(v));
      sz_release(q);
      ready_enqueue(s, f);
      return 0;
    }
    f->state = FIB_QWAIT;
    f->qwait = q;
    fiber_set_cur(f, NULL);
    queue_waiter_add(q, f);
    return 0;
  }
  case SZ_IO_DEFERRED_GET: {
    SzDeferred *d = cur->as.deferred_get;
    cur->as.deferred_get = NULL;
    if (!d) {
      fiber_finish(s, f, 0, NULL, sz_error_new(1, "null deferred"));
      return 0;
    }
    if (d->completed) {
      if (!d->ok) {
        fiber_fail(s, f, deferred_copy_error(d));
        sz_release(d);
        return 0;
      }
      fiber_set_pure_retained(f, d->value);
      sz_release(d);
      ready_enqueue(s, f);
      return 0;
    }
    f->state = FIB_DWAIT;
    f->dwait = d;
    fiber_set_cur(f, NULL);
    def_waiter_add(d, f);
    return 0;
  }
  case SZ_IO_POLL_FD: {
    struct pollfd pfd;
    int n;
    pfd.fd = cur->as.poll.fd;
    pfd.events = (short)cur->as.poll.events;
    pfd.revents = 0;
    n = poll(&pfd, 1, 0);
    if (n < 0 && errno != EINTR) {
      fiber_fail(s, f, sz_error_new(6, "poll failed"));
      return 0;
    }
    if (n > 0 && (pfd.revents & POLLNVAL)) {
      fiber_fail(s, f, sz_error_new(6, "poll failed"));
      return 0;
    }
    if (n > 0) {
      fiber_set_cur(f, pure_drop(NULL));
      ready_enqueue(s, f);
      return 0;
    }
    f->poll_fd = cur->as.poll.fd;
    f->poll_events = cur->as.poll.events;
    f->state = FIB_POLL;
    fiber_set_cur(f, NULL);
    poller_add(s, f);
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
      fiber_set_cur(f, pure_drop(NULL));
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

enum { SZ_POLL_CAP = 64 };

static int fill_pollfds(Sched *s, struct pollfd *pfds, Fiber **fibs, int cap) {
  Fiber *f = s->pollers;
  int n = 0;
  while (f && n < cap) {
    pfds[n].fd = f->poll_fd;
    pfds[n].events = (short)(f->poll_events ? f->poll_events : POLLIN);
    pfds[n].revents = 0;
    fibs[n] = f;
    n++;
    f = f->wait_next;
  }
  return n;
}

static int wake_pollers(Sched *s, struct pollfd *pfds, Fiber **fibs, int n) {
  int i;
  int woke = 0;
  for (i = 0; i < n; i++) {
    Fiber *f = fibs[i];
    short rev;
    if (!f || f->state != FIB_POLL)
      continue;
    rev = pfds[i].revents;
    if (!rev)
      continue;
    poller_remove(s, f);
    if (rev & POLLNVAL) {
      fiber_fail(s, f, sz_error_new(6, "poll failed"));
    } else {
      fiber_set_cur(f, pure_drop(NULL));
      ready_enqueue(s, f);
    }
    woke = 1;
  }
  return woke;
}

static int idle_advance(Sched *s) {
  for (;;) {
    int64_t now = sz_clock_monotonic_ms_sync();
    int64_t next = next_wake_at(s);
    int64_t delta;
    struct pollfd pfds[SZ_POLL_CAP];
    Fiber *fibs[SZ_POLL_CAP];
    int npoll;
    int timeout_ms;
    int pr;
    if (next >= 0 && next <= now)
      return wake_sleepers(s, now);
    npoll = fill_pollfds(s, pfds, fibs, SZ_POLL_CAP);
    if (npoll <= 0 && next < 0)
      return 0;
    if (npoll <= 0) {
      delta = next - now;
      if (sz_testrt_clock_is_fake()) {
        sz_testrt_clock_advance(delta);
        now = sz_clock_monotonic_ms_sync();
        return wake_sleepers(s, now);
      }
      {
        struct timespec ts;
        struct timespec rem;
        ts.tv_sec = (time_t)(delta / 1000);
        ts.tv_nsec = (long)((delta % 1000) * 1000000L);
        if (nanosleep(&ts, &rem) < 0 && errno == EINTR)
          continue;
      }
      now = sz_clock_monotonic_ms_sync();
      if (wake_sleepers(s, now))
        return 1;
      continue;
    }
    if (next < 0)
      timeout_ms = -1;
    else {
      delta = next - now;
      if (delta < 0)
        delta = 0;
      if (delta > 86400000)
        delta = 86400000;
      timeout_ms = (int)delta;
    }
    pr = poll(pfds, (nfds_t)npoll, timeout_ms);
    if (pr < 0 && errno == EINTR)
      continue;
    now = sz_clock_monotonic_ms_sync();
    if (pr > 0 && wake_pollers(s, pfds, fibs, npoll))
      return 1;
    if (wake_sleepers(s, now))
      return 1;
    if (pr < 0)
      return 0;
  }
}

static void cancel_forked_orphans(Sched *s) {
  Fiber *f = s->forked_live;
  while (f) {
    Fiber *next = f->forked_next;
    if (f->state != FIB_DONE && f->state != FIB_CANCELLED)
      fiber_cancel(s, f);
    f = next;
  }
}

static void fiber_release_result(Fiber *f) {
  void *v = f->result_value;
  SzError *err = f->result_error;
  f->result_value = NULL;
  f->result_error = NULL;
  sz_release(v);
  sz_error_free(err);
}

static void sched_free_fibers(Sched *s) {
  Fiber *f = s->all_fibers;
  s->all_fibers = NULL;
  s->root = NULL;
  while (f) {
    Fiber *n = f->all_next;
    sz_release(f->cur);
    f->cur = NULL;
    cont_free_all(f->stack);
    f->stack = NULL;
    fiber_release_result(f);
    sz_free(f);
    f = n;
  }
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
  sched_arm_from_env(&sched);
  g_sched = &sched;
  sched.root = fiber_new(root, NULL, JOIN_NONE, 0);
  ready_enqueue(&sched, sched.root);

  for (;;) {
    Fiber *f = ready_dequeue(&sched);
    if (f) {
      if (f->state == FIB_CANCELLED || f->state == FIB_DONE)
        continue;
      step_fiber(&sched, f);
      /* Drain ready (including cancel finalizers) before exiting on root done. */
      continue;
    }
    if (sched.root->state == FIB_DONE || sched.root->state == FIB_CANCELLED) {
      cancel_forked_orphans(&sched);
      if (sched.ready_head)
        continue;
      break;
    }
    if (idle_advance(&sched))
      continue;
    /* Deadlock: no ready fibers and nothing to wake. */
    break;
  }

  if (sched.root->state == FIB_DONE && sched.root->result_ok) {
    result.ok = 1;
    result.value = sched.root->result_value;
    sched.root->result_value = NULL;
  } else if (sched.root->state == FIB_DONE) {
    result.ok = 0;
    result.error = sched.root->result_error;
    sched.root->result_error = NULL;
    if (!result.error)
      result.error = sz_error_new(1, "IO failed");
  } else {
    fiber_cancel(&sched, sched.root);
    result.ok = 0;
    result.error = sz_error_new(8, "IO deadlock (parked with no wakeup)");
  }

  g_sched = NULL;
  sched_free_fibers(&sched);
  return result;
}

SzIoResult sz_io_unsafe_run(SzIo *root) { return run_io(root); }

/* UI callbacks (taps, Signal.map over IO) have no error channel. An unhandled
   failure mirrors main: report and die so fuzz/tests observe it. */
void *sz_io_unsafe_run_or_die(SzIo *root) {
  SzIoResult r = run_io(root);
  if (!r.ok) {
    fprintf(stderr, "scuzz: IO failed in UI callback: %s\n",
            r.error ? sz_string_cstr(r.error->message) : "unknown");
    fflush(stderr);
    if (r.error)
      sz_error_free(r.error);
    exit(1);
  }
  return r.value;
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
  sz_law_sometimes_flush();
  if (!r.ok) {
    fprintf(stderr, "scuzz: IO failed: %s\n",
            r.error ? sz_string_cstr(r.error->message) : "unknown");
    if (r.error)
      sz_error_free(r.error);
    a->rc = 1;
  } else {
    {
      const char *ds = getenv("SCUZZ_DRIVE_SCRIPT");
      if (ds && ds[0])
        sz_driver_run_script(ds);
    }
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
  /* Run the program on a heap-allocated stack so generated IR (deep but
   * bounded) does not depend on the process main-thread ulimit. Required on
   * macOS. `ulimit -s` cannot grow the main stack. */
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
   * dispatched from the worker can run. A plain pthread_join deadlocks
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
