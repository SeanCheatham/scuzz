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

/* --- ADT boxes (nullary / unary tagged values) --------------------------- */

typedef struct SzAdt {
  int32_t tag;
  void *payload; /* optional; null for nullary cases */
} SzAdt;

SzAdt *sz_adt_new(int32_t tag, void *payload);
int32_t sz_adt_tag(const SzAdt *adt);
void *sz_adt_payload(const SzAdt *adt);

/* --- IO fiber skeleton + blessed kit ------------------------------------ */

typedef struct SzIo SzIo;
typedef struct SzLangResource SzLangResource;
typedef struct SzRef SzRef;
typedef struct SzDeferred SzDeferred;
typedef struct SzQueue SzQueue;
typedef struct SzStream SzStream;

typedef void *(*SzThunk)(void *env);
typedef SzIo *(*SzCont)(void *value, void *env);
typedef int64_t (*SzStreamPred)(void *value, void *env);
typedef void *(*SzStreamMapFn)(void *value, void *env);
typedef SzIo *(*SzErrorHandler)(SzError *err, void *env);

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
  SZ_IO_ENSURE,
  SZ_IO_QUEUE_TAKE,
  SZ_IO_DEFERRED_GET,
  SZ_IO_POLL_FD,
  SZ_IO_TIMEOUT,
  SZ_IO_FORK,
  SZ_IO_JOIN,
  SZ_IO_INTERRUPT,
  SZ_IO_FOREVER,
  SZ_IO_REPEAT_N,
  SZ_IO_RETRY_N
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
    struct {
      SzIo *inner;
      SzIo *finalizer;
    } ensure;
    struct {
      int64_t ms;
      SzIo *inner;
    } timeout;
    struct {
      int64_t n; /* extra repeats / retries; unused for forever */
      SzIo *inner;
    } loop;
    SzIo *fork_inner;
    void *fiber; /* SZ_IO_JOIN / SZ_IO_INTERRUPT handle from Fiber.fork */
    SzQueue *queue_take;
    SzDeferred *deferred_get;
    struct {
      int fd;
      int events;
    } poll;
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
/* Run finalizer after inner succeeds, fails, or is cancelled (race loser). */
SzIo *sz_io_ensure(SzIo *inner, SzIo *finalizer);
/* First-to-settle of sleep(ms) vs inner; timer wins → fail "timeout" and cancel inner. */
SzIo *sz_io_timeout(int64_t ms, SzIo *inner);
/* Rerun inner until it fails or the fiber is cancelled. Never succeeds. */
SzIo *sz_io_forever(SzIo *inner);
/* Run inner once, then n extra times (n<0 → 0 extra). Last success value. */
SzIo *sz_io_repeat_n(int64_t n, SzIo *inner);
/* On failure, retry n extra times (n<0 → 0 extra). Last error if all fail. */
SzIo *sz_io_retry_n(int64_t n, SzIo *inner);
/* Fork inner onto the cooperative scheduler; succeeds immediately with a fiber handle. */
SzIo *sz_fiber_fork(SzIo *inner);
/* Park until the forked fiber succeeds (value) or fails / is interrupted. */
SzIo *sz_fiber_join(void *fiber);
/* Cancel the fiber (ensure/Resource finalizers) and complete after it settles. */
SzIo *sz_fiber_interrupt(void *fiber);
/* Internal constructors used by queue/deferred kits. */
SzIo *sz_io_queue_take(SzQueue *q);
SzIo *sz_io_deferred_get(SzDeferred *d);
SzIo *sz_io_poll_readable(int fd); /* IO[Unit]; park until fd is readable */
SzIo *sz_io_poll_writable(int fd); /* IO[Unit]; park until fd is writable */

/* Run to completion on the calling thread.
 * Concurrency is cooperative single-threaded fibers: sleep/Queue/Deferred/poll
 * park (Net, Sys.readLine, Sys.exec, httpGet DNS). race/both fork left-then-right onto a
 * ready queue. forever/repeatN/retryN rerun the same inner tree (yield each iteration).
 * Fiber.fork starts a supervised child (join parks; interrupt cancels;
 * unjoined children are cancelled when the root fiber completes). Live / default: FIFO pick. When SCUZZ_SCHED_SEED is set (fuzz),
 * ready-fiber pick among n>1 is seed-driven (Lehmer/MINSTD). TestRuntime jumps
 * virtual time to the next wakeup when all fibers are blocked on timers. Live
 * idle wait uses poll (and interruptible nanosleep when only timers remain) so
 * a cancelled sleeper or a ready listen socket cannot hold the run loop. */
typedef struct SzIoResult {
  int ok; /* 1 success, 0 error */
  void *value;
  SzError *error;
} SzIoResult;

SzIoResult sz_io_unsafe_run(SzIo *io);
void *sz_io_unsafe_run_or_die(SzIo *io);

/* Called from queue/deferred delay thunks to wake a parked fiber (returns 1 if woke). */
int sz_fiber_wake_queue(SzQueue *q, void *value);
void sz_fiber_wake_deferred(SzDeferred *d);

/* Language Resource.make / use: IO acquire + SzCont release/use (String payload). */
struct SzLangResource {
  SzIo *acquire;
  SzCont release;
  void *release_env;
};

SzLangResource *sz_lang_resource_make(SzIo *acquire, SzCont release,
                                      void *release_env);
SzIo *sz_lang_resource_use(SzLangResource *res, SzCont use, void *use_env);
void sz_lang_resource_free(SzLangResource *res);

/* Ref — mutable cell (single-threaded). */
struct SzRef {
  void *value;
};

SzRef *sz_ref_make(void *initial);
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

/* Stream — finite pull: emit / eval / concat / map / evalMap / filter / take / takeWhile / drop / dropWhile / find / exists. */
struct SzStream {
  int tag; /* 0 nil … 9 takeWhile, 10 dropWhile, 11 find */
  void *left;
  void *right;
  void *env;
};

SzStream *sz_stream_nil(void);
SzStream *sz_stream_emit(void *value);
SzStream *sz_stream_emits(SzList *xs);
SzStream *sz_stream_eval(SzIo *io);
SzStream *sz_stream_concat(SzStream *left, SzStream *right);
SzStream *sz_stream_evalmap(SzStream *inner, SzCont f, void *env);
SzStream *sz_stream_filter(SzStream *inner, SzStreamPred pred, void *env);
SzStream *sz_stream_map(SzStream *inner, SzStreamMapFn f, void *env);
SzStream *sz_stream_takewhile(SzStream *inner, SzStreamPred pred, void *env);
SzStream *sz_stream_dropwhile(SzStream *inner, SzStreamPred pred, void *env);
SzStream *sz_stream_find(SzStream *inner, SzStreamPred pred, void *env);
SzIo *sz_stream_exists(SzStream *s, SzStreamPred pred, void *env); /* IO[Bool] */
SzStream *sz_stream_take(SzStream *inner, int64_t n);
SzStream *sz_stream_drop(SzStream *inner, int64_t n);
SzIo *sz_stream_compile_to_list(SzStream *s); /* IO[List] */
SzIo *sz_stream_drain(SzStream *s);           /* IO[Unit] */

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

/* Blessed filesystem IO (live or TestRuntime mem FS; chosen when the IO runs) */
SzIo *sz_fs_read(SzString *path);
SzIo *sz_fs_write(SzString *path, SzString *contents);
SzIo *sz_fs_list(SzString *path);
SzIo *sz_fs_mkdirs(SzString *path);
SzIo *sz_fs_canonicalize(SzString *path);

/* Process / args / env / console for Stage-1 CLI + clang (console out = IO.println) */
void sz_sys_set_args(int argc, char **argv);
SzIo *sz_sys_args(void);
SzIo *sz_sys_read_line(void); /* IO[String]: one stdin line; EOF → ""; parks on poll */
SzIo *sz_sys_exec(SzString *cmd); /* IO[Int] exit code; parks on poll until the child exits */
SzIo *sz_sys_spawn(SzString *cmd); /* IO[Int] pid; does not wait */
SzIo *sz_sys_alive(int64_t pid);   /* IO[Int] 1 if running */
SzIo *sz_sys_getenv(SzString *key);

/* Blessed Clock / Random / Net (impurity boundary) */
SzIo *sz_clock_real_time(void);   /* IO[Int] wall epoch ms */
SzIo *sz_clock_monotonic(void);   /* IO[Int] monotonic ms */
int64_t sz_clock_monotonic_ms_sync(void); /* sync read for UI pump dt */

SzIo *sz_random_next_int(int64_t bound); /* IO[Int] in [0, bound) */

SzIo *sz_net_http_get(SzString *url); /* IO[String] body; A+AAAA; 250ms v6 preference; CNAME */
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env); /* IO[Unit]; one GET; 127.0.0.1 + ::1 */
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env); /* IO[Unit]; keep listen; 127.0.0.1 + ::1 */
/* Test-only: UDP nameserver for live httpGet DNS. NULL ip restores /etc/resolv.conf. */
void sz_net_test_set_nameserver(const char *ipv4, int port);

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
void sz_testrt_net_inject_request(const char *path); /* replace queue with one GET path */
void sz_testrt_net_queue_request(const char *path);  /* append a GET path */
int sz_testrt_net_serve_pending(void);
char *sz_testrt_net_pop_request(void); /* owned; NULL if empty */
int sz_testrt_net_is_fake(void);
SzIo *sz_testrt_net_http_get(SzString *url);
const char *sz_testrt_net_last_serve_body(void);
void sz_testrt_net_set_last_serve_body(const char *body);

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
void sz_law_check(SzString *name, int64_t ok);
void sz_law_sometimes(SzString *name);
void sz_law_sometimes_flush(void);
void sz_driver_register(SzString *name, int64_t nargs, int64_t kind, void *fn);
void sz_driver_run_line(const char *spec);

/* Entrypoint helper used by @main codegen */
int sz_runtime_main(SzIo *program);
int sz_runtime_main_args(SzIo *program, int argc, char **argv);

/* Impurity kit + TestRuntime (Clock/Random/Fs/Net/Sys console fakes) */
SzIo *sz_impurity_run_kit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_RT_H */
