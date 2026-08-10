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

/* String ops for Stage-1 / Phase 4 kernel dialect */
SuString *su_string_concat(const SuString *a, const SuString *b);
int64_t su_string_len(const SuString *s);
SuString *su_string_slice(const SuString *s, int64_t start, int64_t end);
int su_string_eq(const SuString *a, const SuString *b);
int64_t su_string_char_at(const SuString *s, int64_t index); /* byte as i64; -1 OOB */
SuString *su_string_from_int(int64_t n);
int64_t su_string_index_of(const SuString *s, const SuString *needle);
SuString *su_string_to_cstr_clone(const SuString *s); /* alias helper */

typedef struct SuList SuList;
/* Split on `\n` / `\r\n`; skip empty lines. */
SuList *su_string_lines(const SuString *s);

/* Boxed i64 for IO[Int] */
void *su_box_i64(int64_t n);
int64_t su_unbox_i64(const void *p);

/* --- errors -------------------------------------------------------------- */

typedef struct SuError {
  int32_t code;
  SuString *message;
} SuError;

SuError *su_error_new(int32_t code, const char *msg);
void su_error_free(SuError *err);

/* --- Either (attempt results) -------------------------------------------- */

typedef struct SuEither {
  int is_right; /* 1 = Right(value), 0 = Left(error) */
  union {
    SuError *left;
    void *right;
  } as;
} SuEither;

SuEither *su_either_right(void *value);
SuEither *su_either_left(SuError *err);
void su_either_free(SuEither *e);

/* --- ADT boxes (nullary / unary tagged values for Stage-0 enums) --------- */

typedef struct SuAdt {
  int32_t tag;
  void *payload; /* optional; null for nullary cases */
} SuAdt;

SuAdt *su_adt_new(int32_t tag, void *payload);
int32_t su_adt_tag(const SuAdt *adt);
void su_adt_free(SuAdt *adt);

/* Lexer.classify — Stage-0 helper for ScalUI parser bootstrap (Tok tags).
 * Tag order: AtMain=0, Def=1, Ident=2, StringLit=3, Eof=4, Other=5 */
SuAdt *su_lexer_classify(const char *source);

/* --- IO fiber skeleton + Phase 3 blessed kit ----------------------------- */

typedef struct SuIo SuIo;
typedef struct SuResource SuResource;
typedef struct SuRef SuRef;
typedef struct SuDeferred SuDeferred;
typedef struct SuQueue SuQueue;

typedef void *(*SuThunk)(void *env);
typedef SuIo *(*SuCont)(void *value, void *env);
typedef SuIo *(*SuErrorHandler)(SuError *err, void *env);
typedef void *(*SuAcquire)(void *env);
typedef void (*SuRelease)(void *value, void *env);

typedef enum SuIoTag {
  SU_IO_PURE = 1,
  SU_IO_DELAY,
  SU_IO_FLATMAP,
  SU_IO_FAIL,
  SU_IO_PRINTLN,
  SU_IO_HANDLE_ERROR,
  SU_IO_ATTEMPT,
  SU_IO_SLEEP_MS,
  SU_IO_RACE,
  SU_IO_BOTH
} SuIoTag;

struct SuIo {
  SuIoTag tag;
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
    struct {
      SuIo *inner;
      SuErrorHandler handler;
      void *env;
    } handle_error;
    SuIo *attempt_inner;
    int64_t sleep_ms;
    struct {
      SuIo *left;
      SuIo *right;
    } race;
    struct {
      SuIo *left;
      SuIo *right;
    } both;
  } as;
};

SuIo *su_io_pure(void *value);
SuIo *su_io_delay(SuThunk thunk, void *env);
SuIo *su_io_flatmap(SuIo *inner, SuCont cont, void *env);
SuIo *su_io_fail(SuError *err);
SuIo *su_io_fail_cstr(const char *msg);
SuIo *su_io_println(SuString *msg);
SuIo *su_io_println_cstr(const char *msg);
SuIo *su_io_handle_error_with(SuIo *inner, SuErrorHandler handler, void *env);
SuIo *su_io_attempt(SuIo *inner);
SuIo *su_io_sleep_ms(int64_t ms);
SuIo *su_io_race(SuIo *left, SuIo *right);
SuIo *su_io_both(SuIo *left, SuIo *right);

/* Run to completion on the calling thread (cooperative fibers for race/both). */
typedef struct SuIoResult {
  int ok; /* 1 success, 0 error */
  void *value;
  SuError *error;
} SuIoResult;

SuIoResult su_io_unsafe_run(SuIo *io);
void su_io_free(SuIo *io);

/* Resource: acquire / release with bracket semantics (releases on failure). */
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

/* Ref — mutable cell (single-threaded; string-friendly for kernel demos). */
struct SuRef {
  void *value;
};

SuRef *su_ref_make(void *initial);
void su_ref_free(SuRef *ref);
SuIo *su_ref_of(void *initial);          /* IO[Ref] */
SuIo *su_ref_of_cstr(const char *initial);
SuIo *su_ref_get(SuRef *ref);            /* IO[A] */
SuIo *su_ref_set(SuRef *ref, void *value); /* IO[Unit] */
SuIo *su_ref_set_cstr(SuRef *ref, const char *value);

/* Deferred — one-shot promise. */
struct SuDeferred {
  int completed;
  int ok;
  void *value;
  SuError *error;
};

SuDeferred *su_deferred_make(void);
void su_deferred_free(SuDeferred *d);
SuIo *su_deferred_empty(void); /* IO[Deferred] */
SuIo *su_deferred_complete(SuDeferred *d, void *value);
SuIo *su_deferred_complete_cstr(SuDeferred *d, const char *value);
SuIo *su_deferred_fail(SuDeferred *d, SuError *err);
SuIo *su_deferred_get(SuDeferred *d); /* IO[A]; fails if incomplete or erred */

/* Queue — unbounded FIFO of void* (strings in kernel demos). */
struct SuQueue {
  void **items;
  size_t len;
  size_t cap;
};

SuQueue *su_queue_make(void);
void su_queue_free(SuQueue *q);
SuIo *su_queue_unbounded(void); /* IO[Queue] */
SuIo *su_queue_offer(SuQueue *q, void *value);
SuIo *su_queue_offer_cstr(SuQueue *q, const char *value);
SuIo *su_queue_take(SuQueue *q); /* IO[A]; fails if empty (Phase 3 sync take) */
size_t su_queue_size(const SuQueue *q);

/* Pair for IO.both results */
typedef struct SuPair {
  void *left;
  void *right;
} SuPair;

SuPair *su_pair_new(void *left, void *right);
void su_pair_free(SuPair *p);

/* Linked list (NULL = Nil) for Phase 4 dialect / Stage-1 compiler */
struct SuList {
  void *head;
  struct SuList *tail;
};

SuList *su_list_nil(void);
int su_list_is_empty(const SuList *xs);
SuList *su_list_cons(void *head, SuList *tail);
void *su_list_head(const SuList *xs);
SuList *su_list_tail(const SuList *xs);
size_t su_list_len(const SuList *xs);
void *su_list_at(const SuList *xs, size_t index);
SuList *su_list_reverse(SuList *xs);
SuList *su_list_append(SuList *xs, void *x);
SuString *su_list_join(const SuList *xs, const char *sep);

/* Blessed filesystem IO (live or TestRuntime mem FS) */
SuIo *su_fs_read(SuString *path);
SuIo *su_fs_write(SuString *path, SuString *contents);
SuIo *su_fs_list(SuString *path);
SuIo *su_fs_mkdirs(SuString *path);

/* Process / args / env for Stage-1 CLI + clang (console out = IO.println) */
void su_sys_set_args(int argc, char **argv);
SuIo *su_sys_args(void);
SuIo *su_sys_exec(SuString *cmd);
SuIo *su_sys_getenv(SuString *key);

/* Blessed Clock / Random / Net (Phase 6 impurity boundary) */
SuIo *su_clock_real_time(void);   /* IO[Int] wall epoch ms */
SuIo *su_clock_monotonic(void);   /* IO[Int] monotonic ms */
int64_t su_clock_monotonic_ms_sync(void); /* sync read for UI pump dt */
void su_clock_sleep_ms(int64_t ms); /* used by IO.sleep run loop */

SuIo *su_random_next_int(int64_t bound); /* IO[Int] in [0, bound) */

SuIo *su_net_http_get(SuString *url); /* IO[String] response body */

/* TestRuntime — fake interpreters for deterministic scalui test / unit tests */
void su_testrt_install(void); /* fake clock+rng+mem FS+stub net */
void su_testrt_reset(void);   /* restore live interpreters */

void su_testrt_clock_install(int64_t start_ms);
void su_testrt_clock_advance(int64_t ms);
int su_testrt_clock_is_fake(void);
int64_t su_testrt_clock_now_ms(void);

void su_testrt_random_install(uint64_t seed);
int su_testrt_random_is_fake(void);

void su_testrt_fs_install(void);
int su_testrt_fs_is_fake(void);
/* Mem-FS IO used by fs.c when fake is active (same SuError codes as live). */
SuIo *su_testrt_fs_read(SuString *path);
SuIo *su_testrt_fs_write(SuString *path, SuString *contents);
SuIo *su_testrt_fs_list(SuString *path);
SuIo *su_testrt_fs_mkdirs(SuString *path);

void su_testrt_net_install(void);
void su_testrt_net_stub(const char *url, const char *body);
int su_testrt_net_is_fake(void);
SuIo *su_testrt_net_http_get(SuString *url);

/* Entrypoint helper used by @main codegen */
int su_runtime_main(SuIo *program);
int su_runtime_main_args(SuIo *program, int argc, char **argv);

/* Kernel demo: blessed effects kit smoke (Ref/Deferred/Queue/race/sleep/errors) */
SuIo *su_effects_run_kit(void);

/* Kernel demo: Phase 6 impurity + TestRuntime (Clock/Random/Fs/Net fakes) */
SuIo *su_impurity_run_kit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCALUI_RT_H */
