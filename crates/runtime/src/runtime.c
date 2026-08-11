#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

void sz_io_free(SzIo *io) {
  /* Shallow free; graphs are short-lived process heaps. */
  sz_free(io);
}

/* --- trampolined run loop with error handlers + cooperative race/both ---- */

typedef enum ContKind { CONT_FLATMAP = 1, CONT_HANDLE = 2 } ContKind;

typedef struct ContFrame {
  ContKind kind;
  SzCont cont;
  SzErrorHandler handler;
  void *env;
  struct ContFrame *next;
} ContFrame;

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

static void sleep_ms(int64_t ms) {
  /* Routed through Clock interpreter (live nanosleep or fake advance). */
  sz_clock_sleep_ms(ms);
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

/* Nested run used by race/both — shares no stack with caller. */
static SzIoResult run_io(SzIo *root);

static SzIoResult run_io(SzIo *root) {
  SzIoResult result;
  result.ok = 0;
  result.value = NULL;
  result.error = NULL;

  if (!root) {
    result.error = sz_error_new(1, "null IO");
    return result;
  }

  ContFrame *stack = NULL;
  SzIo *cur = root;

  for (;;) {
    switch (cur->tag) {
    case SZ_IO_PURE: {
      void *value = cur->as.pure_value;
      while (stack && stack->kind == CONT_HANDLE) {
        /* Success bypasses error handlers. */
        stack = cont_pop(stack);
      }
      if (!stack) {
        result.ok = 1;
        result.value = value;
        return result;
      }
      {
        SzCont cont = stack->cont;
        void *env = stack->env;
        stack = cont_pop(stack);
        cur = cont(value, env);
        if (!cur) {
          cont_free_all(stack);
          result.error = sz_error_new(2, "flatMap continuation returned null");
          return result;
        }
      }
      break;
    }
    case SZ_IO_DELAY: {
      void *value = cur->as.delay.thunk(cur->as.delay.env);
      cur = sz_io_pure(value);
      break;
    }
    case SZ_IO_PRINTLN: {
      const char *s = sz_string_cstr(cur->as.println);
      if (sz_testrt_sys_is_fake())
        sz_testrt_stdout_append(s);
      fputs(s, stdout);
      fputc('\n', stdout);
      fflush(stdout);
      cur = sz_io_pure(NULL);
      break;
    }
    case SZ_IO_SLEEP_MS: {
      sleep_ms(cur->as.sleep_ms);
      cur = sz_io_pure(NULL);
      break;
    }
    case SZ_IO_FAIL: {
      SzError *err = cur->as.fail ? cur->as.fail : sz_error_new(3, "unknown failure");
      /* Unwind to nearest handleErrorWith. */
      while (stack) {
        if (stack->kind == CONT_HANDLE) {
          SzErrorHandler handler = stack->handler;
          void *env = stack->env;
          stack = cont_pop(stack);
          cur = handler(err, env);
          if (!cur) {
            cont_free_all(stack);
            result.error = sz_error_new(2, "error handler returned null");
            return result;
          }
          goto continue_loop;
        }
        stack = cont_pop(stack);
      }
      result.ok = 0;
      result.error = err;
      return result;
    }
    case SZ_IO_FLATMAP: {
      stack = cont_push_flatmap(stack, cur->as.flatmap.cont, cur->as.flatmap.env);
      cur = cur->as.flatmap.inner;
      if (!cur) {
        cont_free_all(stack);
        result.error = sz_error_new(5, "flatMap inner is null");
        return result;
      }
      break;
    }
    case SZ_IO_HANDLE_ERROR: {
      stack = cont_push_handle(stack, cur->as.handle_error.handler,
                               cur->as.handle_error.env);
      cur = cur->as.handle_error.inner;
      if (!cur) {
        cont_free_all(stack);
        result.error = sz_error_new(5, "handleErrorWith inner is null");
        return result;
      }
      break;
    }
    case SZ_IO_ATTEMPT: {
      /* attempt(io) = handleErrorWith(io.map(Right), e => pure(Left(e))) */
      SzIo *inner = cur->as.attempt_inner;
      SzIo *mapped = sz_io_flatmap(inner, attempt_ok, NULL);
      cur = sz_io_handle_error_with(mapped, attempt_err, NULL);
      break;
    }
    case SZ_IO_RACE: {
      /* Prefer the non-sleep side first so IO.race(sleep, println) wins println. */
      SzIo *first = cur->as.race.left;
      SzIo *second = cur->as.race.right;
      if (first->tag == SZ_IO_SLEEP_MS && second->tag != SZ_IO_SLEEP_MS) {
        first = cur->as.race.right;
        second = cur->as.race.left;
      }
      {
        SzIoResult a = run_io(first);
        if (a.ok) {
          cur = sz_io_pure(a.value);
          break;
        }
        {
          SzIoResult b = run_io(second);
          if (a.error)
            sz_error_free(a.error);
          if (b.ok) {
            cur = sz_io_pure(b.value);
            break;
          }
          cont_free_all(stack);
          result.ok = 0;
          result.error =
              b.error ? b.error : sz_error_new(6, "race both failed");
          return result;
        }
      }
    }
    case SZ_IO_BOTH: {
      SzIoResult a = run_io(cur->as.both.left);
      if (!a.ok) {
        cont_free_all(stack);
        result.ok = 0;
        result.error = a.error ? a.error : sz_error_new(7, "both left failed");
        return result;
      }
      {
        SzIoResult b = run_io(cur->as.both.right);
        if (!b.ok) {
          cont_free_all(stack);
          result.ok = 0;
          result.error =
              b.error ? b.error : sz_error_new(7, "both right failed");
          return result;
        }
        cur = sz_io_pure(sz_pair_new(a.value, b.value));
      }
      break;
    }
    default:
      cont_free_all(stack);
      result.error = sz_error_new(4, "invalid IO tag");
      return result;
    }
  continue_loop:;
  }
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
    return NULL;
  }
  a->rc = 0;
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
  perr = pthread_create(&thr, &attr, sz_runtime_main_worker, &args);
  pthread_attr_destroy(&attr);
  if (perr != 0) {
    sz_runtime_main_worker(&args);
    return args.rc;
  }
  perr = pthread_join(thr, NULL);
  if (perr != 0)
    sz_panic("pthread_join failed");
  return args.rc;
}
