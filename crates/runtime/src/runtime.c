#include "scalui_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* --- errors -------------------------------------------------------------- */

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

/* --- IO constructors ----------------------------------------------------- */

static SuIo *su_io_new(int tag) {
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

void su_io_free(SuIo *io) {
  /* Phase 0: shallow free only; graphs are short-lived process heaps. */
  su_free(io);
}

/* --- trampolined run loop (single-threaded; CE-inspired) ----------------- */

typedef struct ContFrame {
  SuCont cont;
  void *env;
  struct ContFrame *next;
} ContFrame;

static ContFrame *cont_push(ContFrame *stack, SuCont cont, void *env) {
  ContFrame *f = (ContFrame *)su_alloc(sizeof(ContFrame));
  f->cont = cont;
  f->env = env;
  f->next = stack;
  return f;
}

static ContFrame *cont_pop(ContFrame *stack, SuCont *cont, void **env) {
  ContFrame *next = stack->next;
  *cont = stack->cont;
  *env = stack->env;
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

SuIoResult su_io_unsafe_run(SuIo *root) {
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
      if (!stack) {
        result.ok = 1;
        result.value = value;
        return result;
      }
      {
        SuCont cont;
        void *env;
        stack = cont_pop(stack, &cont, &env);
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
      if (!stack) {
        result.ok = 1;
        result.value = value;
        return result;
      }
      {
        SuCont cont;
        void *env;
        stack = cont_pop(stack, &cont, &env);
        cur = cont(value, env);
        if (!cur) {
          cont_free_all(stack);
          result.error = su_error_new(2, "flatMap continuation returned null");
          return result;
        }
      }
      break;
    }
    case SU_IO_PRINTLN: {
      fputs(su_string_cstr(cur->as.println), stdout);
      fputc('\n', stdout);
      fflush(stdout);
      if (!stack) {
        result.ok = 1;
        result.value = NULL;
        return result;
      }
      {
        SuCont cont;
        void *env;
        stack = cont_pop(stack, &cont, &env);
        cur = cont(NULL, env);
        if (!cur) {
          cont_free_all(stack);
          result.error = su_error_new(2, "flatMap continuation returned null");
          return result;
        }
      }
      break;
    }
    case SU_IO_FAIL: {
      SuError *err = cur->as.fail;
      cont_free_all(stack);
      result.ok = 0;
      result.error = err ? err : su_error_new(3, "unknown failure");
      return result;
    }
    case SU_IO_FLATMAP: {
      stack = cont_push(stack, cur->as.flatmap.cont, cur->as.flatmap.env);
      cur = cur->as.flatmap.inner;
      if (!cur) {
        cont_free_all(stack);
        result.error = su_error_new(5, "flatMap inner is null");
        return result;
      }
      break;
    }
    default:
      cont_free_all(stack);
      result.error = su_error_new(4, "invalid IO tag");
      return result;
    }
  }
}

int su_runtime_main(SuIo *program) {
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
