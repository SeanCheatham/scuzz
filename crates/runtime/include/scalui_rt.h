#ifndef SCALUI_RT_H
#define SCALUI_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- panic / alloc ------------------------------------------------------- */

void su_panic(const char *msg) __attribute__((noreturn));
void *su_alloc(size_t size);
void *su_alloc_zero(size_t size);
void su_free(void *ptr);

/* --- strings (UTF-8, length-prefixed, null-terminated for C interop) ----- */

typedef struct SuString {
  size_t len;
  char *data; /* len bytes + trailing NUL */
} SuString;

SuString *su_string_from_cstr(const char *cstr);
SuString *su_string_from_bytes(const char *bytes, size_t len);
const char *su_string_cstr(const SuString *s);
void su_string_free(SuString *s);

/* --- errors -------------------------------------------------------------- */

typedef struct SuError {
  int32_t code;
  SuString *message;
} SuError;

SuError *su_error_new(int32_t code, const char *msg);
void su_error_free(SuError *err);

/* --- IO fiber skeleton --------------------------------------------------- */

typedef struct SuIo SuIo;
typedef struct SuResource SuResource;

typedef void *(*SuThunk)(void *env);
typedef SuIo *(*SuCont)(void *value, void *env);
typedef void *(*SuAcquire)(void *env);
typedef void (*SuRelease)(void *value, void *env);

struct SuIo {
  enum {
    SU_IO_PURE = 1,
    SU_IO_DELAY,
    SU_IO_FLATMAP,
    SU_IO_FAIL,
    SU_IO_PRINTLN
  } tag;
  union {
    void *pure_value;
    struct {
      SuThunk thunk;
      void *env;
    } delay;
    struct {
      SuIo *inner;
      SuCont cont;
      void *env;
    } flatmap;
    SuError *fail;
    SuString *println;
  } as;
};

SuIo *su_io_pure(void *value);
SuIo *su_io_delay(SuThunk thunk, void *env);
SuIo *su_io_flatmap(SuIo *inner, SuCont cont, void *env);
SuIo *su_io_fail(SuError *err);
SuIo *su_io_println(SuString *msg);
SuIo *su_io_println_cstr(const char *msg);

/* Run to completion on the calling thread (Phase 0 single-threaded loop). */
typedef struct SuIoResult {
  int ok; /* 1 success, 0 error */
  void *value;
  SuError *error;
} SuIoResult;

SuIoResult su_io_unsafe_run(SuIo *io);
void su_io_free(SuIo *io);

/* Resource: acquire / release with bracket semantics */
struct SuResource {
  SuAcquire acquire;
  SuRelease release;
  void *env;
  SuIo *(*use)(void *acquired, void *use_env);
  void *use_env;
};

SuResource *su_resource_make(SuAcquire acquire, SuRelease release, void *env);
SuIo *su_resource_use(SuResource *res, SuIo *(*use)(void *acquired, void *env),
                      void *use_env);
void su_resource_free(SuResource *res);

/* Entrypoint helper used by @main codegen */
int su_runtime_main(SuIo *program);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_RT_H */
