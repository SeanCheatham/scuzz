#ifndef SCUZZ_RT_H
#define SCUZZ_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- panic / alloc ------------------------------------------------------- */

void sz_panic(const char *msg) __attribute__((noreturn));
void *sz_alloc(size_t size);
void *sz_alloc_zero(size_t size);
void sz_free(void *ptr);
/* Live heap via sz_alloc/sz_free (user bytes; excludes size header). */
void sz_alloc_stats(size_t *live_bytes, size_t *live_count);
/* Reset peak / pump-sample counter; live stays accurate for outstanding allocs. */
void sz_alloc_reset_stats(void);
/* Optional SCUZZ_ALLOC_TRACE=1 sample from sz_ui_pump_sync (every N pumps). */
void sz_alloc_trace_on_pump(void);

/* --- strings (UTF-8, length-prefixed, null-terminated for C interop) ----- */

typedef struct SzString {
  size_t len;
  char *data; /* len bytes + trailing NUL */
} SzString;

SzString *sz_string_from_cstr(const char *cstr);
SzString *sz_string_from_bytes(const char *bytes, size_t len);
const char *sz_string_cstr(const SzString *s);
void sz_string_free(SzString *s);

/* String ops for the kernel dialect */
SzString *sz_string_concat(const SzString *a, const SzString *b);
int64_t sz_string_len(const SzString *s);
SzString *sz_string_slice(const SzString *s, int64_t start, int64_t end);
int sz_string_eq(const SzString *a, const SzString *b);
int64_t sz_string_char_at(const SzString *s, int64_t index); /* byte as i64; -1 OOB */
SzString *sz_string_from_int(int64_t n);
int64_t sz_string_index_of(const SzString *s, const SzString *needle);

typedef struct SzList SzList;
/* Split on `\n` / `\r\n`; skip empty lines. */
SzList *sz_string_lines(const SzString *s);

/* Boxed i64 for IO[Int] */
void *sz_box_i64(int64_t n);
int64_t sz_unbox_i64(const void *p);

/* --- errors -------------------------------------------------------------- */

typedef struct SzError {
  int32_t code;
  SzString *message;
} SzError;

SzError *sz_error_new(int32_t code, const char *msg);
void sz_error_free(SzError *err);

/* --- Either (attempt results) -------------------------------------------- */

typedef struct SzEither {
  int is_right; /* 1 = Right(value), 0 = Left(error) */
  union {
    SzError *left;
    void *right;
  } as;
} SzEither;

SzEither *sz_either_right(void *value);
SzEither *sz_either_left(SzError *err);
void sz_either_free(SzEither *e);

/* --- ADT boxes (nullary / unary tagged values for Stage-0 enums) --------- */

typedef struct SzAdt {
  int32_t tag;
  void *payload; /* optional; null for nullary cases */
} SzAdt;

SzAdt *sz_adt_new(int32_t tag, void *payload);
int32_t sz_adt_tag(const SzAdt *adt);
void *sz_adt_payload(const SzAdt *adt);
void sz_adt_free(SzAdt *adt);

/* --- IO fiber skeleton + blessed kit ------------------------------------ */

typedef struct SzIo SzIo;
typedef struct SzResource SzResource;
typedef struct SzRef SzRef;
typedef struct SzDeferred SzDeferred;
typedef struct SzQueue SzQueue;

typedef void *(*SzThunk)(void *env);
typedef SzIo *(*SzCont)(void *value, void *env);
typedef SzIo *(*SzErrorHandler)(SzError *err, void *env);
typedef void *(*SzAcquire)(void *env);
typedef void (*SzRelease)(void *value, void *env);

typedef enum SzIoTag {
  SZ_IO_PURE = 1,
  SZ_IO_DELAY,
  SZ_IO_FLATMAP,
  SZ_IO_FAIL,
  SZ_IO_PRINTLN,
  SZ_IO_HANDLE_ERROR,
  SZ_IO_ATTEMPT,
  SZ_IO_SLEEP_MS,
  SZ_IO_RACE,
  SZ_IO_BOTH,
  SZ_IO_QUEUE_TAKE,
  SZ_IO_DEFERRED_GET
} SzIoTag;

struct SzIo {
  SzIoTag tag;
  union {
    void *pure_value;
    struct {
      SzThunk thunk;
      void *env;
    } delay;
    struct {
      SzIo *inner;
      SzCont cont;
      void *env;
    } flatmap;
    SzError *fail;
    SzString *println;
    struct {
      SzIo *inner;
      SzErrorHandler handler;
      void *env;
    } handle_error;
    SzIo *attempt_inner;
    int64_t sleep_ms;
    struct {
      SzIo *left;
      SzIo *right;
    } race;
    struct {
      SzIo *left;
      SzIo *right;
    } both;
    SzQueue *queue_take;
    SzDeferred *deferred_get;
  } as;
};

SzIo *sz_io_pure(void *value);
SzIo *sz_io_delay(SzThunk thunk, void *env);
SzIo *sz_io_flatmap(SzIo *inner, SzCont cont, void *env);
SzIo *sz_io_fail(SzError *err);
SzIo *sz_io_fail_cstr(const char *msg);
SzIo *sz_io_println(SzString *msg);
SzIo *sz_io_println_cstr(const char *msg);
SzIo *sz_io_handle_error_with(SzIo *inner, SzErrorHandler handler, void *env);
SzIo *sz_io_attempt(SzIo *inner);
SzIo *sz_io_sleep_ms(int64_t ms);
SzIo *sz_io_race(SzIo *left, SzIo *right);
SzIo *sz_io_both(SzIo *left, SzIo *right);
/* Internal constructors used by queue/deferred kits. */
SzIo *sz_io_queue_take(SzQueue *q);
SzIo *sz_io_deferred_get(SzDeferred *d);

/* Run to completion on the calling thread.
 * Concurrency is cooperative single-threaded fibers: sleep/Queue/Deferred park;
 * race/both fork left-then-right onto a ready queue. Live / default: FIFO pick.
 * When SCUZZ_SCHED_SEED is set (fuzz), ready-fiber pick among n>1 is seed-driven
 * (Lehmer/MINSTD). TestRuntime jumps virtual time to the next wakeup when all
 * fibers are blocked on timers. Live idle sleep is interruptible (EINTR
 * re-checks soonest wake so a cancelled sleeper cannot hold the run loop). */
typedef struct SzIoResult {
  int ok; /* 1 success, 0 error */
  void *value;
  SzError *error;
} SzIoResult;

SzIoResult sz_io_unsafe_run(SzIo *io);
void *sz_io_unsafe_run_or_die(SzIo *io);
void sz_io_free(SzIo *io);

/* Called from queue/deferred delay thunks to wake a parked fiber (returns 1 if woke). */
int sz_fiber_wake_queue(SzQueue *q, void *value);
void sz_fiber_wake_deferred(SzDeferred *d);

/* Resource: acquire / release with bracket semantics (releases on failure). */
struct SzResource {
  SzAcquire acquire;
  SzRelease release;
  void *env;
  SzIo *(*use)(void *acquired, void *use_env);
  void *use_env;
};

SzResource *sz_resource_make(SzAcquire acquire, SzRelease release, void *env);
SzIo *sz_resource_use(SzResource *res, SzIo *(*use)(void *acquired, void *env),
                      void *use_env);
void sz_resource_free(SzResource *res);

/* Ref — mutable cell (single-threaded). */
struct SzRef {
  void *value;
};

SzRef *sz_ref_make(void *initial);
void sz_ref_free(SzRef *ref);
SzIo *sz_ref_of(void *initial);          /* IO[Ref] */
SzIo *sz_ref_of_cstr(const char *initial);
SzIo *sz_ref_get(SzRef *ref);            /* IO[A] */
SzIo *sz_ref_set(SzRef *ref, void *value); /* IO[Unit] */
SzIo *sz_ref_set_cstr(SzRef *ref, const char *value);

/* Deferred — one-shot promise. */
struct SzDeferred {
  int completed;
  int ok;
  void *value;
  SzError *error;
  void *waiters; /* runtime Fiber* list while get is parked */
};

SzDeferred *sz_deferred_make(void);
void sz_deferred_free(SzDeferred *d);
SzIo *sz_deferred_empty(void); /* IO[Deferred] */
SzIo *sz_deferred_complete(SzDeferred *d, void *value);
SzIo *sz_deferred_complete_cstr(SzDeferred *d, const char *value);
SzIo *sz_deferred_fail(SzDeferred *d, SzError *err);
SzIo *sz_deferred_get(SzDeferred *d); /* IO[A]; parks until complete under the fiber scheduler */

/* Queue — unbounded FIFO of void*. */
struct SzQueue {
  void **items;
  size_t len;
  size_t cap;
  void *waiters; /* runtime Fiber* list while take is parked */
};

SzQueue *sz_queue_make(void);
void sz_queue_free(SzQueue *q);
SzIo *sz_queue_unbounded(void); /* IO[Queue] */
SzIo *sz_queue_offer(SzQueue *q, void *value);
SzIo *sz_queue_offer_cstr(SzQueue *q, const char *value);
SzIo *sz_queue_take(SzQueue *q); /* IO[A]; parks when empty under the fiber scheduler */
size_t sz_queue_size(const SzQueue *q);

/* Pair for IO.both results */
typedef struct SzPair {
  void *left;
  void *right;
} SzPair;

SzPair *sz_pair_new(void *left, void *right);
void sz_pair_free(SzPair *p);

/* Linked list (NULL = Nil) for kernel dialect / Stage-1 compiler */
struct SzList {
  void *head;
  struct SzList *tail;
};

SzList *sz_list_nil(void);
int sz_list_is_empty(const SzList *xs);
SzList *sz_list_cons(void *head, SzList *tail);
void *sz_list_head(const SzList *xs);
SzList *sz_list_tail(const SzList *xs);
size_t sz_list_len(const SzList *xs);
void *sz_list_at(const SzList *xs, size_t index);
SzList *sz_list_reverse(SzList *xs);
SzList *sz_list_append(SzList *xs, void *x);
/* Free cons cells only; does not free heads. */
void sz_list_free(SzList *xs);
SzString *sz_list_join(const SzList *xs, const char *sep);

/* Blessed filesystem IO (live or TestRuntime mem FS) */
SzIo *sz_fs_read(SzString *path);
SzIo *sz_fs_write(SzString *path, SzString *contents);
SzIo *sz_fs_list(SzString *path);
SzIo *sz_fs_mkdirs(SzString *path);
SzIo *sz_fs_canonicalize(SzString *path);

/* Process / args / env / console for Stage-1 CLI + clang (console out = IO.println) */
void sz_sys_set_args(int argc, char **argv);
SzIo *sz_sys_args(void);
SzIo *sz_sys_read_line(void); /* IO[String]: one stdin line; EOF → "" */
SzIo *sz_sys_exec(SzString *cmd);
SzIo *sz_sys_getenv(SzString *key);

/* Blessed Clock / Random / Net (impurity boundary) */
SzIo *sz_clock_real_time(void);   /* IO[Int] wall epoch ms */
SzIo *sz_clock_monotonic(void);   /* IO[Int] monotonic ms */
int64_t sz_clock_monotonic_ms_sync(void); /* sync read for UI pump dt */
void sz_clock_sleep_ms(int64_t ms); /* blocking sleep; EINTR restarts remaining */

SzIo *sz_random_next_int(int64_t bound); /* IO[Int] in [0, bound) */

SzIo *sz_net_http_get(SzString *url); /* IO[String] response body */

/* TestRuntime — fake interpreters for deterministic scuzz test / unit tests */
void sz_testrt_install(void); /* fake clock+rng+mem FS+stub net+sys/console */
void sz_testrt_reset(void);   /* restore live interpreters */

void sz_testrt_clock_install(int64_t start_ms);
void sz_testrt_clock_advance(int64_t ms);
int sz_testrt_clock_is_fake(void);
int64_t sz_testrt_clock_now_ms(void);

void sz_testrt_random_install(uint64_t seed);
int sz_testrt_random_is_fake(void);

void sz_testrt_fs_install(void);
int sz_testrt_fs_is_fake(void);
/* Mem-FS IO used by fs.c when fake is active (same SzError codes as live). */
SzIo *sz_testrt_fs_read(SzString *path);
SzIo *sz_testrt_fs_write(SzString *path, SzString *contents);
SzIo *sz_testrt_fs_list(SzString *path);
SzIo *sz_testrt_fs_mkdirs(SzString *path);
SzIo *sz_testrt_fs_canonicalize(SzString *path);

void sz_testrt_net_install(void);
void sz_testrt_net_stub(const char *url, const char *body);
int sz_testrt_net_is_fake(void);
SzIo *sz_testrt_net_http_get(SzString *url);

/* Console fakes: scripted stdin, optional argv override, println capture (+ echo). */
void sz_testrt_sys_install(void);
void sz_testrt_sys_reset_live(void);
int sz_testrt_sys_is_fake(void);
void sz_testrt_sys_set_args(int argc, char **argv); /* user args only; overrides live */
int sz_testrt_sys_has_args_override(void);
SzList *sz_testrt_sys_args_list(void); /* snapshot of override (or empty) */
void sz_testrt_stdin_feed(const char *text); /* newline-separated lines */
SzIo *sz_testrt_sys_read_line(void);
void sz_testrt_stdout_reset(void);
void sz_testrt_stdout_append(const char *line); /* appends line + '\n' */
const char *sz_testrt_stdout_cstr(void);

/* Laws — residual checks armed only under SCUZZ_TESTRT=1 */
void sz_law_stash_a11y(const char *dump);
int64_t sz_law_a11y_has(SzString *needle);
SzIo *sz_law_assert(SzString *name, int64_t ok);

/* Entrypoint helper used by @main codegen */
int sz_runtime_main(SzIo *program);
int sz_runtime_main_args(SzIo *program, int argc, char **argv);

/* Impurity kit + TestRuntime (Clock/Random/Fs/Net/Sys console fakes) */
SzIo *sz_impurity_run_kit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_RT_H */
