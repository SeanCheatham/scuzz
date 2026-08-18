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
/* RC objects (strings, list cells, ADTs, boxed i64, map/set nodes, IO,
 * streams, resources, errors, Ref / Queue / Deferred, Either, pair). List cells retain heads and shared tails. IO
 * constructors take child IO nodes. Non-RC sz_alloc pointers no-op. */
enum {
  SZ_RC_STRING = 1,
  SZ_RC_LIST = 2,
  SZ_RC_ADT = 3,
  SZ_RC_BOX = 4,
  SZ_RC_MAP = 5,
  SZ_RC_IO = 6,
  SZ_RC_STREAM = 7,
  SZ_RC_RESOURCE = 8,
  SZ_RC_ERROR = 9,
  SZ_RC_REF = 10,
  SZ_RC_QUEUE = 11,
  SZ_RC_DEFERRED = 12,
  SZ_RC_EITHER = 13,
  SZ_RC_PAIR = 14
};
void *sz_rc_alloc(size_t size, uint32_t kind);
void sz_retain(void *ptr);
void sz_release(void *ptr);
/* Live heap through sz_alloc/sz_free (user bytes; excludes size header). */
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
SzString *sz_string_from_float(double x);
int64_t sz_string_index_of(const SzString *s, const SzString *needle);
int64_t sz_string_starts_with(const SzString *s, const SzString *prefix);
SzString *sz_string_trim(const SzString *s);

typedef struct SzList SzList;
typedef struct SzMap SzMap;
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
/* Caller owns a ref. A non-null error shares its message. */
SzString *sz_error_message(const SzError *err);
int32_t sz_error_code(const SzError *err);

/* --- Either (attempt results) -------------------------------------------- */

typedef struct SzEither {
  int is_right; /* 1 = Right(value), 0 = Left(error) */
  union {
    SzError *left;
    void *right;
  } as;
} SzEither;

/* Callee retains the value. Caller drops after the call. */
SzEither *sz_either_right(void *value);
/* Callee retains the error. Caller drops after the call. */
SzEither *sz_either_left(SzError *err);
/* Last sz_release drops the Right value or the Left error, then frees the cell. */
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

/* Callee retains the value. Caller drops after the call. Last sz_release of
 * the IO node drops a leftover payload. Run retains so last-use of the result
 * does not free the node slot. */
SzIo *sz_io_pure(void *value);
/* Callee retains a RC env. Caller drops after the call. Last sz_release of
 * the delay node drops leftover RC env or a leftover sz_alloc env. It does
 * not load an RC header from a string literal or a small integer. Run steals
 * env so the thunk owns it. */
SzIo *sz_io_delay(SzThunk thunk, void *env);
SzIo *sz_io_flatmap(SzIo *inner, SzCont cont, void *env);
/* Callee retains the error. Caller drops after the call. Last sz_release of
 * the IO node drops a leftover error. Run retains so fiber_fail does not
 * free the node slot. */
SzIo *sz_io_fail(SzError *err);
SzIo *sz_io_fail_cstr(const char *msg);
SzIo *sz_io_println(SzString *msg);
SzIo *sz_io_println_cstr(const char *msg);
SzIo *sz_io_handle_error_with(SzIo *inner, SzErrorHandler handler, void *env);
SzIo *sz_io_attempt(SzIo *inner);
SzIo *sz_io_attempt_as_result(SzIo *inner);
SzAdt *sz_either_to_result(SzEither *e);
SzIo *sz_io_sleep_ms(int64_t ms);
SzIo *sz_io_race(SzIo *left, SzIo *right);
SzIo *sz_io_both(SzIo *left, SzIo *right);
/* Run finalizer after inner succeeds, fails, or is cancelled (race loser). */
SzIo *sz_io_ensure(SzIo *inner, SzIo *finalizer);
/* First-to-settle of sleep(ms) vs inner. Timer wins → fail "timeout" and cancel inner. */
SzIo *sz_io_timeout(int64_t ms, SzIo *inner);
/* Rerun inner until it fails or the fiber is cancelled. Never succeeds. */
SzIo *sz_io_forever(SzIo *inner);
/* Run inner once, then n extra times (n<0 → 0 extra). Last success value. */
SzIo *sz_io_repeat_n(int64_t n, SzIo *inner);
/* On failure, retry n extra times (n<0 → 0 extra). Last error if all fail. */
SzIo *sz_io_retry_n(int64_t n, SzIo *inner);
/* Fork inner onto the cooperative scheduler. Succeeds at once with a fiber handle. */
SzIo *sz_fiber_fork(SzIo *inner);
/* Park until the forked fiber succeeds (value) or fails / is interrupted.
 * Join retains the value so last-use does not free the fiber slot. */
SzIo *sz_fiber_join(void *fiber);
/* Cancel the fiber (ensure/Resource finalizers) and complete after it settles. */
SzIo *sz_fiber_interrupt(void *fiber);
/* Internal constructors used by queue/deferred kits. */
SzIo *sz_io_queue_take(SzQueue *q);
SzIo *sz_io_deferred_get(SzDeferred *d);
SzIo *sz_io_poll_readable(int fd); /* IO[Unit]; park until fd is readable */
SzIo *sz_io_poll_writable(int fd); /* IO[Unit]; park until fd is writable */

/* Run to completion on the calling thread. Cooperative single-threaded fibers. */
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

/* Callee retains the initial value. Caller drops after the call. */
SzRef *sz_ref_make(void *initial);
/* Release the current value, then free the cell. */
void sz_ref_free(SzRef *r);
SzIo *sz_ref_of(void *initial);          /* IO[Ref] */
SzIo *sz_ref_of_cstr(const char *initial);
/* Get retains the current value. Caller drops the run result. */
SzIo *sz_ref_get(SzRef *ref);            /* IO[A] */
/* Callee retains the new value. Caller drops after the call. */
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
/* Release a completed value (and a failed error), then free the cell. */
void sz_deferred_free(SzDeferred *d);
SzIo *sz_deferred_empty(void); /* IO[Deferred] */
/* Callee retains the value. Caller drops after the call. */
SzIo *sz_deferred_complete(SzDeferred *d, void *value);
SzIo *sz_deferred_complete_cstr(SzDeferred *d, const char *value);
/* Get retains the completed value. Caller drops the run result. */
SzIo *sz_deferred_get(SzDeferred *d); /* IO[A]; parks until complete under the fiber scheduler */

/* Queue — unbounded FIFO of void*. */
struct SzQueue {
  void **items;
  size_t len;
  size_t cap;
  void *waiters; /* runtime Fiber* list while take is parked */
};

SzQueue *sz_queue_make(void);
/* Release leftover items, then free the queue. */
void sz_queue_free(SzQueue *q);
SzIo *sz_queue_unbounded(void); /* IO[Queue] */
/* Callee retains the value. Caller drops after the call. */
SzIo *sz_queue_offer(SzQueue *q, void *value);
SzIo *sz_queue_offer_cstr(SzQueue *q, const char *value);
/* Take transfers the offer retain. Caller drops the run result. Do not retain again. */
SzIo *sz_queue_take(SzQueue *q); /* IO[A]; parks when empty under the fiber scheduler */
size_t sz_queue_size(const SzQueue *q);

/* Stream — finite pull: emit / eval / concat / map / evalMap / filter / take / takeWhile / drop / dropWhile / find / exists. */
enum {
  SZ_ST_NIL = 0,
  SZ_ST_CONS = 1,
  SZ_ST_EVAL = 2,
  SZ_ST_CONCAT = 3,
  SZ_ST_EVALMAP = 4,
  SZ_ST_TAKE = 5,
  SZ_ST_DROP = 6,
  SZ_ST_FILTER = 7,
  SZ_ST_MAP = 8,
  SZ_ST_TAKEWHILE = 9,
  SZ_ST_DROPWHILE = 10,
  SZ_ST_FIND = 11
};

struct SzStream {
  int tag;
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

/* Callee retains both sides. Caller drops after the call. */
SzPair *sz_pair_new(void *left, void *right);
/* Last sz_release drops both fields, then frees the cell. */
void sz_pair_free(SzPair *p);

/* Linked list (NULL = Nil) */
struct SzList {
  void *head;
  struct SzList *tail;
};

typedef int64_t (*SzListPred)(void *head, void *env);
typedef void *(*SzListMapFn)(void *head, void *env);

SzList *sz_list_nil(void);
int sz_list_is_empty(const SzList *xs);
SzList *sz_list_cons(void *head, SzList *tail);
void *sz_list_head(const SzList *xs);
SzList *sz_list_tail(const SzList *xs);
size_t sz_list_len(const SzList *xs);
void *sz_list_at(const SzList *xs, size_t index);
SzList *sz_list_reverse(SzList *xs);
/* Copy the spine and retain `x` (same as cons). Drop an owned `x` after. */
SzList *sz_list_append(SzList *xs, void *x);
SzList *sz_list_set_at(SzList *xs, int64_t index, void *v);
/* Keep heads for which `pred` is nonzero. New spine; cons retains heads. */
SzList *sz_list_filter(SzList *xs, SzListPred pred, void *env);
/* First n heads as a new spine. n <= 0 is empty. */
SzList *sz_list_take(SzList *xs, int64_t n);
/* Skip n heads and retain the suffix. n <= 0 retains xs. */
SzList *sz_list_drop(SzList *xs, int64_t n);
/* First head for which `pred` is nonzero, as a one-cell list. Empty if none. */
SzList *sz_list_find(SzList *xs, SzListPred pred, void *env);
/* 1 if any head matches `pred`, else 0. */
int64_t sz_list_exists(SzList *xs, SzListPred pred, void *env);
/* New spine. `fn` returns an owned pointer. Retain `head` when `fn` yields it.
 * Cons retains the mapped head. Map then drops the mapper ref. */
SzList *sz_list_map(SzList *xs, SzListMapFn fn, void *env);
/* Release the spine; heads drop through RC. */
void sz_list_free(SzList *xs);
SzString *sz_list_join(const SzList *xs, const char *sep);

/* Persistent Map / Set (NULL = empty). key_kind 0 = boxed i64, 1 = String. */
struct SzMap {
  void *key;
  void *val;
  struct SzMap *left;
  struct SzMap *right;
  int32_t key_kind;
};
SzMap *sz_map_empty(void);
SzMap *sz_map_set(SzMap *m, void *key, void *val, int32_t key_kind);
void *sz_map_get_or(SzMap *m, void *key, void *dflt);
int64_t sz_map_contains(SzMap *m, void *key);

/* Blessed filesystem IO (live or TestRuntime mem FS; chosen when the IO runs) */
SzIo *sz_fs_read(SzString *path);
SzIo *sz_fs_write(SzString *path, SzString *contents);
SzIo *sz_fs_list(SzString *path);
SzIo *sz_fs_mkdirs(SzString *path);
SzIo *sz_fs_canonicalize(SzString *path);

/* Process / args / env / console (console out = IO.println) */
void sz_sys_set_args(int argc, char **argv);
SzIo *sz_sys_args(void);
SzIo *sz_sys_read_line(void); /* IO[String]: one stdin line; EOF → ""; parks on poll */
SzIo *sz_sys_read(int64_t n); /* IO[String]: n stdin bytes (or fewer at EOF); parks on poll */
SzIo *sz_sys_write(SzString *s); /* IO[Unit]: stdout bytes, no newline */
SzIo *sz_sys_exec(SzString *cmd); /* IO[Int] exit code; parks on poll; fails under TestRuntime */
SzIo *sz_sys_spawn(SzString *cmd); /* IO[Int] pid; does not wait; fails under TestRuntime */
SzIo *sz_sys_alive(int64_t pid);   /* IO[Int] 1 if running */
SzIo *sz_sys_kill(int64_t pid);    /* IO[Unit] SIGTERM; no-op if already gone */
SzIo *sz_sys_getenv(SzString *key);

/* Blessed Clock / Random / Net (impurity boundary) */
SzIo *sz_clock_real_time(void);   /* IO[Int] wall epoch ms */
SzIo *sz_clock_monotonic(void);   /* IO[Int] monotonic ms */
int64_t sz_clock_monotonic_ms_sync(void); /* sync read for UI pump dt */

SzIo *sz_random_next_int(int64_t bound); /* IO[Int] in [0, bound) */

SzIo *sz_net_http_get(SzString *url); /* IO[String] body; A+AAAA; 1s DNS/connect/read */
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env); /* IO[Unit]; one GET; 1s req/write; 127.0.0.1 + ::1 */
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env); /* IO[Unit]; keep listen; drop bad clients/handlers */
/* Test-only: UDP nameserver for live httpGet DNS. NULL ip restores /etc/resolv.conf. */
void sz_net_test_set_nameserver(const char *ipv4, int port);

/* TestRuntime — fake interpreters for deterministic scuzz test / fuzz */
void sz_testrt_install(void); /* fake clock+rng+mem FS+stub net+sys/console */
void sz_testrt_reset(void);   /* restore live interpreters */

void sz_testrt_clock_install(int64_t start_ms);
void sz_testrt_clock_advance(int64_t ms);
int sz_testrt_clock_is_fake(void);
int64_t sz_testrt_clock_now_ms(void);

void sz_testrt_random_install(uint64_t seed);

int sz_testrt_fs_is_fake(void);
/* Mem-FS IO used by fs.c when fake is active (same SzError codes as live). */
SzIo *sz_testrt_fs_read(SzString *path);
SzIo *sz_testrt_fs_write(SzString *path, SzString *contents);
SzIo *sz_testrt_fs_list(SzString *path);
SzIo *sz_testrt_fs_mkdirs(SzString *path);
SzIo *sz_testrt_fs_canonicalize(SzString *path);

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
int sz_testrt_sys_is_fake(void);
void sz_testrt_sys_set_args(int argc, char **argv); /* user args only; overrides live */
int sz_testrt_sys_has_args_override(void);
SzList *sz_testrt_sys_args_list(void); /* snapshot of override (or empty) */
void sz_testrt_stdin_feed(const char *text); /* newline-separated lines */
SzIo *sz_testrt_sys_read_line(void);
SzIo *sz_testrt_sys_read(int64_t n);
void sz_testrt_stdout_reset(void);
void sz_testrt_stdout_append(const char *line); /* appends line + '\n' */
void sz_testrt_stdout_write(const char *bytes, size_t n); /* raw; no extra newline */
const char *sz_testrt_stdout_cstr(void);
void sz_testrt_env_set(const char *key, const char *val); /* sealed Sys.getenv map */
const char *sz_testrt_env_get(const char *key); /* NULL if unset */
void sz_testrt_proc_put(int64_t pid); /* fake Sys.alive table */
int sz_testrt_proc_alive(int64_t pid); /* 1 if registered */
void sz_testrt_proc_kill(int64_t pid); /* drop from table */

/* Laws — residual checks armed only under SCUZZ_TESTRT=1 */
void sz_law_stash_a11y(const char *dump);
int64_t sz_law_a11y_has(SzString *needle);
SzIo *sz_law_assert(SzString *name, int64_t ok);
void sz_law_check(SzString *name, int64_t ok);
void sz_law_sometimes(SzString *name);
void sz_law_sometimes_flush(void);
void sz_driver_register(SzString *name, int64_t nargs, int64_t kind, void *fn);
void sz_driver_run_line(const char *spec);
void sz_driver_run_script(const char *path);

/* Entrypoint helper used by @main codegen */
int sz_runtime_main_args(SzIo *program, int argc, char **argv);

/* Impurity kit + TestRuntime (Clock/Random/Fs/Net/Sys console fakes) */
SzIo *sz_impurity_run_kit(void);

#ifdef __cplusplus
}
#endif

#endif /* SCUZZ_RT_H */
