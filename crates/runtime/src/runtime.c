#define _POSIX_C_SOURCE 200809L
#include "scalui_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* --- panic / alloc ------------------------------------------------------- */

void su_panic(const char *msg) {
  fprintf(stderr, "scalui panic: %s\n", msg ? msg : "(null)");
  abort();
}

void *su_alloc(size_t size) {
  void *p = malloc(size);
  if (!p)
    su_panic("out of memory");
  return p;
}

void *su_alloc_zero(size_t size) {
  void *p = calloc(1, size);
  if (!p)
    su_panic("out of memory");
  return p;
}

void su_free(void *ptr) { free(ptr); }

/* --- strings ------------------------------------------------------------- */

SuString *su_string_from_bytes(const char *bytes, size_t len) {
  SuString *s = (SuString *)su_alloc(sizeof(SuString));
  s->len = len;
  s->data = (char *)su_alloc(len + 1);
  if (len)
    memcpy(s->data, bytes, len);
  s->data[len] = '\0';
  return s;
}

SuString *su_string_from_cstr(const char *cstr) {
  if (!cstr)
    su_panic("su_string_from_cstr(null)");
  return su_string_from_bytes(cstr, strlen(cstr));
}

const char *su_string_cstr(const SuString *s) {
  return s && s->data ? s->data : "";
}

void su_string_free(SuString *s) {
  if (!s)
    return;
  su_free(s->data);
  su_free(s);
}

SuString *su_string_concat(const SuString *a, const SuString *b) {
  size_t al = a && a->data ? a->len : 0;
  size_t bl = b && b->data ? b->len : 0;
  char *buf = (char *)su_alloc(al + bl + 1);
  if (al)
    memcpy(buf, a->data, al);
  if (bl)
    memcpy(buf + al, b->data, bl);
  buf[al + bl] = '\0';
  SuString *out = su_string_from_bytes(buf, al + bl);
  su_free(buf);
  return out;
}

int64_t su_string_len(const SuString *s) {
  return s ? (int64_t)s->len : 0;
}

SuString *su_string_slice(const SuString *s, int64_t start, int64_t end) {
  int64_t len = su_string_len(s);
  if (start < 0)
    start = 0;
  if (end > len)
    end = len;
  if (start > end)
    start = end;
  size_t n = (size_t)(end - start);
  if (!s || !s->data || n == 0)
    return su_string_from_cstr("");
  return su_string_from_bytes(s->data + start, n);
}

int su_string_eq(const SuString *a, const SuString *b) {
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  if (a->len != b->len)
    return 0;
  return memcmp(a->data, b->data, a->len) == 0;
}

int64_t su_string_char_at(const SuString *s, int64_t index) {
  if (!s || index < 0 || (size_t)index >= s->len)
    return -1;
  return (unsigned char)s->data[index];
}

SuString *su_string_from_int(int64_t n) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)n);
  return su_string_from_cstr(buf);
}

int64_t su_string_index_of(const SuString *s, const SuString *needle) {
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

void *su_box_i64(int64_t n) {
  int64_t *p = (int64_t *)su_alloc(sizeof(int64_t));
  *p = n;
  return p;
}

int64_t su_unbox_i64(const void *p) {
  return p ? *(const int64_t *)p : 0;
}

/* --- errors / Either / ADT / Pair ---------------------------------------- */

SuError *su_error_new(int32_t code, const char *msg) {
  SuError *e = (SuError *)su_alloc(sizeof(SuError));
  e->code = code;
  e->message = su_string_from_cstr(msg ? msg : "error");
  return e;
}

void su_error_free(SuError *err) {
  if (!err)
    return;
  su_string_free(err->message);
  su_free(err);
}

SuEither *su_either_right(void *value) {
  SuEither *e = (SuEither *)su_alloc_zero(sizeof(SuEither));
  e->is_right = 1;
  e->as.right = value;
  return e;
}

SuEither *su_either_left(SuError *err) {
  SuEither *e = (SuEither *)su_alloc_zero(sizeof(SuEither));
  e->is_right = 0;
  e->as.left = err;
  return e;
}

void su_either_free(SuEither *e) {
  if (!e)
    return;
  if (!e->is_right && e->as.left)
    su_error_free(e->as.left);
  su_free(e);
}

SuAdt *su_adt_new(int32_t tag, void *payload) {
  SuAdt *a = (SuAdt *)su_alloc(sizeof(SuAdt));
  a->tag = tag;
  a->payload = payload;
  return a;
}

int32_t su_adt_tag(const SuAdt *adt) { return adt ? adt->tag : -1; }

void su_adt_free(SuAdt *adt) { su_free(adt); }

/* Tok tags for Lexer.classify / ScalUI parser bootstrap */
enum {
  SU_TOK_AT_MAIN = 0,
  SU_TOK_DEF = 1,
  SU_TOK_IDENT = 2,
  SU_TOK_STRING = 3,
  SU_TOK_EOF = 4,
  SU_TOK_OTHER = 5
};

static int is_ident_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident(char c) {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

SuAdt *su_lexer_classify(const char *source) {
  const char *p = source ? source : "";
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
    p++;
  if (*p == '\0')
    return su_adt_new(SU_TOK_EOF, NULL);
  if (*p == '@') {
    if (strncmp(p, "@main", 5) == 0 && !is_ident(p[5]))
      return su_adt_new(SU_TOK_AT_MAIN, NULL);
    return su_adt_new(SU_TOK_OTHER, NULL);
  }
  if (*p == '"')
    return su_adt_new(SU_TOK_STRING, NULL);
  if (strncmp(p, "def", 3) == 0 && !is_ident(p[3]))
    return su_adt_new(SU_TOK_DEF, NULL);
  if (is_ident_start(*p))
    return su_adt_new(SU_TOK_IDENT, NULL);
  return su_adt_new(SU_TOK_OTHER, NULL);
}

SuPair *su_pair_new(void *left, void *right) {
  SuPair *p = (SuPair *)su_alloc(sizeof(SuPair));
  p->left = left;
  p->right = right;
  return p;
}

void su_pair_free(SuPair *p) { su_free(p); }

/* --- IO constructors ----------------------------------------------------- */

static SuIo *su_io_new(SuIoTag tag) {
  SuIo *io = (SuIo *)su_alloc_zero(sizeof(SuIo));
  io->tag = tag;
  return io;
}

SuIo *su_io_pure(void *value) {
  SuIo *io = su_io_new(SU_IO_PURE);
  io->as.pure_value = value;
  return io;
}

SuIo *su_io_delay(SuThunk thunk, void *env) {
  if (!thunk)
    su_panic("su_io_delay(null thunk)");
  SuIo *io = su_io_new(SU_IO_DELAY);
  io->as.delay.thunk = thunk;
  io->as.delay.env = env;
  return io;
}

SuIo *su_io_flatmap(SuIo *inner, SuCont cont, void *env) {
  if (!inner || !cont)
    su_panic("su_io_flatmap(null)");
  SuIo *io = su_io_new(SU_IO_FLATMAP);
  io->as.flatmap.inner = inner;
  io->as.flatmap.cont = cont;
  io->as.flatmap.env = env;
  return io;
}

SuIo *su_io_fail(SuError *err) {
  SuIo *io = su_io_new(SU_IO_FAIL);
  io->as.fail = err;
  return io;
}

SuIo *su_io_fail_cstr(const char *msg) {
  return su_io_fail(su_error_new(1, msg ? msg : "fail"));
}

SuIo *su_io_println(SuString *msg) {
  if (!msg)
    su_panic("su_io_println(null)");
  SuIo *io = su_io_new(SU_IO_PRINTLN);
  io->as.println = msg;
  return io;
}

SuIo *su_io_println_cstr(const char *msg) {
  return su_io_println(su_string_from_cstr(msg));
}

SuIo *su_io_handle_error_with(SuIo *inner, SuErrorHandler handler, void *env) {
  if (!inner || !handler)
    su_panic("su_io_handle_error_with(null)");
  SuIo *io = su_io_new(SU_IO_HANDLE_ERROR);
  io->as.handle_error.inner = inner;
  io->as.handle_error.handler = handler;
  io->as.handle_error.env = env;
  return io;
}

SuIo *su_io_attempt(SuIo *inner) {
  if (!inner)
    su_panic("su_io_attempt(null)");
  SuIo *io = su_io_new(SU_IO_ATTEMPT);
  io->as.attempt_inner = inner;
  return io;
}

SuIo *su_io_sleep_ms(int64_t ms) {
  SuIo *io = su_io_new(SU_IO_SLEEP_MS);
  io->as.sleep_ms = ms < 0 ? 0 : ms;
  return io;
}

SuIo *su_io_race(SuIo *left, SuIo *right) {
  if (!left || !right)
    su_panic("su_io_race(null)");
  SuIo *io = su_io_new(SU_IO_RACE);
  io->as.race.left = left;
  io->as.race.right = right;
  return io;
}

SuIo *su_io_both(SuIo *left, SuIo *right) {
  if (!left || !right)
    su_panic("su_io_both(null)");
  SuIo *io = su_io_new(SU_IO_BOTH);
  io->as.both.left = left;
  io->as.both.right = right;
  return io;
}

void su_io_free(SuIo *io) {
  /* Phase 0/3: shallow free; graphs are short-lived process heaps. */
  su_free(io);
}

/* --- trampolined run loop with error handlers + cooperative race/both ---- */

typedef enum ContKind { CONT_FLATMAP = 1, CONT_HANDLE = 2 } ContKind;

typedef struct ContFrame {
  ContKind kind;
  SuCont cont;
  SuErrorHandler handler;
  void *env;
  struct ContFrame *next;
} ContFrame;

static ContFrame *cont_push_flatmap(ContFrame *stack, SuCont cont, void *env) {
  ContFrame *f = (ContFrame *)su_alloc(sizeof(ContFrame));
  f->kind = CONT_FLATMAP;
  f->cont = cont;
  f->handler = NULL;
  f->env = env;
  f->next = stack;
  return f;
}

static ContFrame *cont_push_handle(ContFrame *stack, SuErrorHandler handler,
                                   void *env) {
  ContFrame *f = (ContFrame *)su_alloc(sizeof(ContFrame));
  f->kind = CONT_HANDLE;
  f->cont = NULL;
  f->handler = handler;
  f->env = env;
  f->next = stack;
  return f;
}

static ContFrame *cont_pop(ContFrame *stack) {
  ContFrame *next = stack->next;
  su_free(stack);
  return next;
}

static void cont_free_all(ContFrame *stack) {
  while (stack) {
    ContFrame *n = stack->next;
    su_free(stack);
    stack = n;
  }
}

static void sleep_ms(int64_t ms) {
  /* Phase 6: routed through Clock interpreter (live nanosleep or fake advance). */
  su_clock_sleep_ms(ms);
}

/* Attempt success continuation: wrap value as Right. */
static SuIo *attempt_ok(void *value, void *env) {
  (void)env;
  return su_io_pure(su_either_right(value));
}

static SuIo *attempt_err(SuError *err, void *env) {
  (void)env;
  return su_io_pure(su_either_left(err));
}

/* Nested run used by race/both — shares no stack with caller. */
static SuIoResult run_io(SuIo *root);

static SuIoResult run_io(SuIo *root) {
  SuIoResult result;
  result.ok = 0;
  result.value = NULL;
  result.error = NULL;

  if (!root) {
    result.error = su_error_new(1, "null IO");
    return result;
  }

  ContFrame *stack = NULL;
  SuIo *cur = root;

  for (;;) {
    switch (cur->tag) {
    case SU_IO_PURE: {
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
        SuCont cont = stack->cont;
        void *env = stack->env;
        stack = cont_pop(stack);
        cur = cont(value, env);
        if (!cur) {
          cont_free_all(stack);
          result.error = su_error_new(2, "flatMap continuation returned null");
          return result;
        }
      }
      break;
    }
    case SU_IO_DELAY: {
      void *value = cur->as.delay.thunk(cur->as.delay.env);
      cur = su_io_pure(value);
      break;
    }
    case SU_IO_PRINTLN: {
      fputs(su_string_cstr(cur->as.println), stdout);
      fputc('\n', stdout);
      fflush(stdout);
      cur = su_io_pure(NULL);
      break;
    }
    case SU_IO_SLEEP_MS: {
      sleep_ms(cur->as.sleep_ms);
      cur = su_io_pure(NULL);
      break;
    }
    case SU_IO_FAIL: {
      SuError *err = cur->as.fail ? cur->as.fail : su_error_new(3, "unknown failure");
      /* Unwind to nearest handleErrorWith. */
      while (stack) {
        if (stack->kind == CONT_HANDLE) {
          SuErrorHandler handler = stack->handler;
          void *env = stack->env;
          stack = cont_pop(stack);
          cur = handler(err, env);
          if (!cur) {
            cont_free_all(stack);
            result.error = su_error_new(2, "error handler returned null");
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
    case SU_IO_FLATMAP: {
      stack = cont_push_flatmap(stack, cur->as.flatmap.cont, cur->as.flatmap.env);
      cur = cur->as.flatmap.inner;
      if (!cur) {
        cont_free_all(stack);
        result.error = su_error_new(5, "flatMap inner is null");
        return result;
      }
      break;
    }
    case SU_IO_HANDLE_ERROR: {
      stack = cont_push_handle(stack, cur->as.handle_error.handler,
                               cur->as.handle_error.env);
      cur = cur->as.handle_error.inner;
      if (!cur) {
        cont_free_all(stack);
        result.error = su_error_new(5, "handleErrorWith inner is null");
        return result;
      }
      break;
    }
    case SU_IO_ATTEMPT: {
      /* attempt(io) = handleErrorWith(io.map(Right), e => pure(Left(e))) */
      SuIo *inner = cur->as.attempt_inner;
      SuIo *mapped = su_io_flatmap(inner, attempt_ok, NULL);
      cur = su_io_handle_error_with(mapped, attempt_err, NULL);
      break;
    }
    case SU_IO_RACE: {
      /* Prefer the non-sleep side first so IO.race(sleep, println) wins println. */
      SuIo *first = cur->as.race.left;
      SuIo *second = cur->as.race.right;
      if (first->tag == SU_IO_SLEEP_MS && second->tag != SU_IO_SLEEP_MS) {
        first = cur->as.race.right;
        second = cur->as.race.left;
      }
      {
        SuIoResult a = run_io(first);
        if (a.ok) {
          cur = su_io_pure(a.value);
          break;
        }
        {
          SuIoResult b = run_io(second);
          if (a.error)
            su_error_free(a.error);
          if (b.ok) {
            cur = su_io_pure(b.value);
            break;
          }
          cont_free_all(stack);
          result.ok = 0;
          result.error =
              b.error ? b.error : su_error_new(6, "race both failed");
          return result;
        }
      }
    }
    case SU_IO_BOTH: {
      SuIoResult a = run_io(cur->as.both.left);
      if (!a.ok) {
        cont_free_all(stack);
        result.ok = 0;
        result.error = a.error ? a.error : su_error_new(7, "both left failed");
        return result;
      }
      {
        SuIoResult b = run_io(cur->as.both.right);
        if (!b.ok) {
          cont_free_all(stack);
          result.ok = 0;
          result.error =
              b.error ? b.error : su_error_new(7, "both right failed");
          return result;
        }
        cur = su_io_pure(su_pair_new(a.value, b.value));
      }
      break;
    }
    default:
      cont_free_all(stack);
      result.error = su_error_new(4, "invalid IO tag");
      return result;
    }
  continue_loop:;
  }
}

SuIoResult su_io_unsafe_run(SuIo *root) { return run_io(root); }

int su_runtime_main(SuIo *program) {
  return su_runtime_main_args(program, 0, NULL);
}

int su_runtime_main_args(SuIo *program, int argc, char **argv) {
  if (argc > 0 && argv)
    su_sys_set_args(argc, argv);
  /* Phase 6: optional TestRuntime for deterministic replay (set by tests / CI). */
  {
    const char *tr = getenv("SCALUI_TESTRT");
    if (tr && tr[0] == '1')
      su_testrt_install();
  }
  SuIoResult r = su_io_unsafe_run(program);
  if (!r.ok) {
    fprintf(stderr, "scalui: IO failed: %s\n",
            r.error ? su_string_cstr(r.error->message) : "unknown");
    if (r.error)
      su_error_free(r.error);
    return 1;
  }
  return 0;
}
