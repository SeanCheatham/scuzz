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
/* Free every remaining live block. Panic uses this before abort. */
void sz_alloc_sweep(void);
/* Visit each live block. The callback must not alloc or free. */
void sz_alloc_walk(void (*fn)(void *ptr, uint32_t kind, size_t bytes,
                              uint32_t rc, void *ctx),
                   void *ctx);
/* Write live rows (`kind rc=N bytes=M`). `max_rows` < 0 means no cap.
 * Extra rows become `truncated=N`. Truncates to `cap`. Returns bytes
 * written, not counting the NUL. */
int sz_alloc_format_live(char *buf, size_t cap, int max_rows);
/* Write panic text plus `[heap]` and `[live]`. Truncates to `cap`. */
int sz_alloc_format_panic(char *buf, size_t cap, const char *msg);
/* Register a panic dump path. NULL or empty clears it. `SCUZZ_PANIC_DUMP`
 * is used when no path is registered. */
void sz_alloc_set_panic_dump(const char *path);
/* Registered path, or `SCUZZ_PANIC_DUMP`, or NULL. */
const char *sz_alloc_panic_dump_path(void);
/* RC objects (strings, list cells, ADTs, boxed i64, map/set nodes, IO,
 * streams, resources, errors, Ref / Queue / Deferred, Either, pair,
 * Builder, Net sockets). List cells retain heads and shared tails.
 * IO constructors take child IO nodes. Non-RC sz_alloc pointers no-op. */
enum {
  SZ_RC_RAW = 0,
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
  SZ_RC_PAIR = 14,
  SZ_RC_BUILDER = 15,
  SZ_RC_NETSOCK = 16,
  SZ_RC_KIND_COUNT = 17
};
void *sz_rc_alloc(size_t size, uint32_t kind);
void sz_retain(void *ptr);
void sz_release(void *ptr);
/* Live heap through sz_alloc/sz_free (user bytes; excludes size header). */
void sz_alloc_stats(size_t *live_bytes, size_t *live_count);
/* Sum of RC counts on live RC blocks. Raw sz_alloc blocks add 0. */
uint64_t sz_alloc_rc_sum(void);
/* Live bytes and count for one kind (`SZ_RC_RAW` … `SZ_RC_NETSOCK`). */
void sz_alloc_kind_stats(uint32_t kind, size_t *bytes, size_t *count);
/* Dump key for `kind` (`raw`, `string`, …). Unknown kinds use `raw`. */
const char *sz_alloc_kind_name(uint32_t kind);
/* High-water live_bytes since process start or the last reset. */
size_t sz_alloc_peak_bytes(void);
/* Reset peak / pump-sample counter; live stays accurate for outstanding allocs. */
void sz_alloc_reset_stats(void);
/* Snapshot live totals so the next census reports delta from this point. */
void sz_alloc_mark(void);
/* live minus the last mark (may be negative). */
void sz_alloc_delta(int64_t *bytes, int64_t *count);
/* Write census lines (`live_bytes=…` through kind `name=count:bytes`).
 * `mark` 1 snapshots live after the write. Truncates to `cap`. Returns
 * bytes written, not counting the NUL. Live debug dumps also write
 * `[live]` through `sz_alloc_format_live`. */
int sz_alloc_format_heap(char *buf, size_t cap, int mark);
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

typedef struct SzBuilder {
  char *data;
  size_t len;
  size_t cap;
} SzBuilder;

SzBuilder *sz_builder_new(void);
SzBuilder *sz_builder_append(SzBuilder *b, const SzString *s);
SzString *sz_builder_result(const SzBuilder *b);
void sz_builder_free(SzBuilder *b);
/* Closed-form 0+1+…+n for n >= 0. n < 0 is 0. Independent of a Scuzz loop. */
int64_t sz_oracle_sum_to(int64_t n);
int64_t sz_string_len(const SzString *s);
SzString *sz_string_slice(const SzString *s, int64_t start, int64_t end);
int sz_string_eq(const SzString *a, const SzString *b);
int64_t sz_string_char_at(const SzString *s, int64_t index); /* byte as i64; -1 OOB */
SzString *sz_string_from_int(int64_t n);
SzString *sz_string_from_float(double x);
int64_t sz_string_index_of(const SzString *s, const SzString *needle);
int64_t sz_string_last_index_of(const SzString *s, const SzString *needle);
/* First `n` bytes. `n` <= 0 is empty. */
SzString *sz_string_take(const SzString *s, int64_t n);
/* Skip `n` bytes. `n` <= 0 copies `s`. */
SzString *sz_string_drop(const SzString *s, int64_t n);
/* Last `n` bytes. `n` <= 0 is empty. */
SzString *sz_string_take_right(const SzString *s, int64_t n);
/* Drop last `n` bytes. `n` <= 0 copies `s`. */
SzString *sz_string_drop_right(const SzString *s, int64_t n);
/* Reverse bytes. */
SzString *sz_string_reverse(const SzString *s);
int64_t sz_string_starts_with(const SzString *s, const SzString *prefix);
int64_t sz_string_ends_with(const SzString *s, const SzString *suffix);
int64_t sz_string_contains(const SzString *s, const SzString *needle);
/* Whole-string base-10 parse. Leading space is allowed. Junk or overflow uses `dflt`. */
int64_t sz_string_to_int(const SzString *s, int64_t dflt);
/* Replace every non-overlapping `oldv`. Empty `oldv` copies `s`. */
SzString *sz_string_replace(const SzString *s, const SzString *oldv, const SzString *newv);
SzString *sz_string_trim(const SzString *s);
int64_t sz_string_is_empty(const SzString *s);
int64_t sz_string_non_empty(const SzString *s);
/* ASCII A-Z / a-z only. Other bytes stay. */
SzString *sz_string_to_lower(const SzString *s);
SzString *sz_string_to_upper(const SzString *s);
/* First ASCII letter to upper. Other bytes stay. Empty stays empty. */
SzString *sz_string_capitalize(const SzString *s);
/* Repeat `s` `n` times. `n` <= 0 is empty. */
SzString *sz_string_repeat(const SzString *s, int64_t n);
/* Drop `prefix` when `s` starts with it. Else copy `s`. */
SzString *sz_string_strip_prefix(const SzString *s, const SzString *prefix);
/* Drop `suffix` when `s` ends with it. Else copy `s`. */
SzString *sz_string_strip_suffix(const SzString *s, const SzString *suffix);
/* Pad on the left to width `n`. `n` <= len, or empty pad, copies `s`. */
SzString *sz_string_pad_left(const SzString *s, int64_t n, const SzString *pad);
/* Pad on the right to width `n`. `n` <= len, or empty pad, copies `s`. */
SzString *sz_string_pad_right(const SzString *s, int64_t n, const SzString *pad);
/* 1 when `s` is empty or only ASCII space, tab, CR, LF. */
int64_t sz_string_is_blank(const SzString *s);

typedef struct SzList SzList;
typedef struct SzMap SzMap;
/* Split on `\n` / `\r\n`; skip empty lines. */
SzList *sz_string_lines(const SzString *s);
/* Split on non-overlapping `sep`. Empty `sep` copies `s` as one cell. */
SzList *sz_string_split(const SzString *s, const SzString *sep);

/* Boxed i64 for IO[Int] */
void *sz_box_i64(int64_t n);
int64_t sz_unbox_i64(const void *p);
/* 0 when `key` is a boxed Int, else 1 (String). Null is 1. */
int32_t sz_map_infer_key_kind(const void *key);
/* 1 when pointers match, or both strings, boxed i64, lists, maps, ADTs,
 * pairs, Either, or errors match by value. Other RC kinds stay identity. */
int sz_ptr_eq(const void *a, const void *b);

/* --- errors -------------------------------------------------------------- */

typedef struct SzError {
  int32_t code;
  SzString *message;
} SzError;

SzError *sz_error_new(int32_t code, const char *msg);
void sz_error_free(SzError *err);
/* Caller owns a ref. A non-null error shares its message. */
SzString *sz_error_message(const SzError *err);

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
typedef void *(*SzListMapFn)(void *head, void *env);
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
 * the IO node drops a leftover RC payload. Delay result boxes are RC; unwrap
 * takes ok/err and drops the box. Run retains so last-use of the result does
 * not free the node slot. */
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
/* Run f on each list cell in order. Collects results. Empty is IO.pure(nil).
 * Failure or cancel stops later cells. Callee retains xs and env. */
SzIo *sz_io_foreach(SzList *xs, SzCont f, void *env);
/* Same walk. Discards each success value. Result is IO[Unit]. */
SzIo *sz_io_foreach_discard(SzList *xs, SzCont f, void *env);
/* If cond is non-zero, return inner. Else IO.pure(Unit). Caller drops inner. */
SzIo *sz_io_when(int64_t cond, SzIo *inner);
/* If cond is zero, return inner. Else IO.pure(Unit). Caller drops inner. */
SzIo *sz_io_unless(int64_t cond, SzIo *inner);
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
/* Snapshot the cooperative scheduler. `parked` is every live fiber that is
 * not READY and not DONE/CANCELLED. NULL out-params are ignored. */
void sz_fiber_census(int64_t *live, int64_t *ready, int64_t *parked,
                     int64_t *done);

/* Language Resource.make / use: IO acquire + SzCont release/use. */
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
/* Mapper returns an owned pointer. The slot takes that ref. */
SzIo *sz_ref_update(SzRef *ref, SzListMapFn fn, void *env); /* IO[Unit] */
SzIo *sz_ref_update_and_get(SzRef *ref, SzListMapFn fn, void *env); /* IO[A] */

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
/* Sync complete. No-op when already completed. Wakes parked gets. */
void sz_deferred_complete_now(SzDeferred *d, void *value);
/* Sync fail. Retains `err`. No-op when already completed. Wakes parked gets. */
void sz_deferred_fail_now(SzDeferred *d, SzError *err);
SzIo *sz_deferred_fail(SzDeferred *d, SzError *err); /* IO[Unit] */
/* Get retains the completed value. Caller drops the run result. */
SzIo *sz_deferred_get(SzDeferred *d); /* IO[A]; parks until complete under the fiber scheduler */

/* Queue — unbounded FIFO of void*. Items sit in a ring. Waiters are FIFO
 * (oldest parked take gets the next offer). */
struct SzQueue {
  void **items;
  size_t head; /* index of the oldest item; unused when len is 0 */
  size_t len;
  size_t cap;
  void *waiters; /* runtime Fiber* FIFO while take is parked */
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
/* Transfer an owned retain into the ring. Does not wake waiters. */
void sz_queue_enqueue(SzQueue *q, void *value);
/* Test hook: wake one parked take, then cancel that fiber before it steps.
 * Returns 1 if a waiter was cancelled. The value retain transfers like offer. */
int sz_queue_cancel_ready_handoff(SzQueue *q, void *value);

/* Stream — finite pull: emit / eval / concat / map / evalMap / filter /
 * take / takeWhile / drop / dropWhile / find / exists / range / repeatN /
 * zip / zipWithIndex / interleave / intersperse / grouped / flatten /
 * flatMap / scan / fold / forall / changes / filterNot / mapConcat /
 * zipWith / zipAll / orElse / sliding / takeRight / dropRight / findLast /
 * evalTap / iterate / unfold / head / last / count / none. */
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
  SZ_ST_FIND = 11,
  SZ_ST_FLATMAP = 12,
  SZ_ST_ZIP = 13,
  SZ_ST_SCAN = 14,
  SZ_ST_GROUPED = 15,
  SZ_ST_INTERSPERSE = 16,
  SZ_ST_ZIPIDX = 17,
  SZ_ST_FLATTEN = 18,
  SZ_ST_CHANGES = 19,
  SZ_ST_REPEATN = 20,
  SZ_ST_FILTERNOT = 21,
  SZ_ST_MAPCONCAT = 22,
  SZ_ST_ZIPWITH = 23,
  SZ_ST_ZIPALL = 24,
  SZ_ST_ORELSE = 25,
  SZ_ST_SLIDING = 26,
  SZ_ST_TAKERIGHT = 27,
  SZ_ST_DROPRIGHT = 28,
  SZ_ST_FINDLAST = 29,
  SZ_ST_EVALTAP = 30,
  SZ_ST_INTERLEAVE = 31
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
/* Boxed ints `[from, until)`. Empty when `until` <= `from`. */
SzStream *sz_stream_range(int64_t from, int64_t until);
/* Concat `inner` with itself `n` times. `n` <= 0 is empty. */
SzStream *sz_stream_repeat_n(SzStream *inner, int64_t n);
SzStream *sz_stream_zip(SzStream *left, SzStream *right); /* pairs; stops at shorter */
SzStream *sz_stream_interleave(SzStream *left, SzStream *right); /* a0,b0,a1,b1,… */
SzStream *sz_stream_zip_with_index(SzStream *inner); /* (Int, A) pairs */
SzStream *sz_stream_intersperse(SzStream *inner, void *sep);
SzStream *sz_stream_grouped(SzStream *inner, int64_t n); /* Stream[List[A]] */
SzStream *sz_stream_flatten(SzStream *inner); /* Stream[List[A]] -> Stream[A] */
SzStream *sz_stream_flatmap(SzStream *inner, SzStreamMapFn f, void *env);
SzStream *sz_stream_scan(SzStream *inner, void *z, SzStreamMapFn f, void *env);
SzStream *sz_stream_changes(SzStream *inner);
SzIo *sz_stream_fold(SzStream *s, void *z, SzStreamMapFn f, void *env); /* IO[Z] */
SzIo *sz_stream_forall(SzStream *s, SzStreamPred pred, void *env); /* IO[Bool] */
SzStream *sz_stream_filter_not(SzStream *inner, SzStreamPred pred, void *env);
SzStream *sz_stream_map_concat(SzStream *inner, SzStreamMapFn f, void *env);
SzStream *sz_stream_zip_with(SzStream *left, SzStream *right, SzStreamMapFn f,
                            void *env);
SzStream *sz_stream_zip_all(SzStream *left, SzStream *right, void *pad_left,
                           void *pad_right);
SzStream *sz_stream_or_else(SzStream *left, SzStream *right);
SzStream *sz_stream_sliding(SzStream *inner, int64_t n);
SzStream *sz_stream_take_right(SzStream *inner, int64_t n);
SzStream *sz_stream_drop_right(SzStream *inner, int64_t n);
SzStream *sz_stream_find_last(SzStream *inner, SzStreamPred pred, void *env);
SzStream *sz_stream_evaltap(SzStream *inner, SzCont f, void *env);
/* Emit `z`, then `f(z)`, n times. n <= 0 is empty. */
SzStream *sz_stream_iterate(void *z, int64_t n, SzStreamMapFn f, void *env);
/* `f` returns List of 0 or 1 `(A, Z)` pair. Empty stops. Caps at 65536. */
SzStream *sz_stream_unfold(void *z, SzStreamMapFn f, void *env);
SzIo *sz_stream_head(SzStream *s);  /* IO[A]; fails when empty */
SzIo *sz_stream_last(SzStream *s);  /* IO[A]; fails when empty */
SzIo *sz_stream_count(SzStream *s); /* IO[Int] */
SzIo *sz_stream_none(SzStream *s, SzStreamPred pred, void *env); /* IO[Bool] */
SzIo *sz_stream_compile_to_list(SzStream *s); /* IO[List] */
SzIo *sz_stream_drain(SzStream *s);           /* IO[Unit] */

/* Pair for IO.both results */
typedef struct SzPair {
  void *left;
  void *right;
} SzPair;

/* Callee retains both sides. Caller drops after the call. */
SzPair *sz_pair_new(void *left, void *right);
/* Borrow a slot. The pair keeps the retain. */
void *sz_pair_left(const SzPair *p);
void *sz_pair_right(const SzPair *p);
/* Last sz_release drops both fields, then frees the cell. */
void sz_pair_free(SzPair *p);

/* Linked list (NULL = Nil) */
struct SzList {
  void *head;
  struct SzList *tail;
};

typedef int64_t (*SzListPred)(void *head, void *env);

SzList *sz_list_nil(void);
int sz_list_is_empty(const SzList *xs);
SzList *sz_list_cons(void *head, SzList *tail);
void *sz_list_head(const SzList *xs);
SzList *sz_list_tail(const SzList *xs);
int64_t sz_list_len(const SzList *xs);
void *sz_list_at(const SzList *xs, int64_t index);
SzList *sz_list_reverse(SzList *xs);
/* Copy the spine and retain `x` (same as cons). Drop an owned `x` after. */
SzList *sz_list_append(SzList *xs, void *x);
SzList *sz_list_set_at(SzList *xs, int64_t index, void *v);
/* Keep heads for which `pred` is nonzero. New spine; cons retains heads. */
SzList *sz_list_filter(SzList *xs, SzListPred pred, void *env);
/* Keep heads for which `pred` is zero. */
SzList *sz_list_filter_not(SzList *xs, SzListPred pred, void *env);
/* Count heads for which `pred` is nonzero. */
int64_t sz_list_count(SzList *xs, SzListPred pred, void *env);
/* First n heads as a new spine. n <= 0 is empty. */
SzList *sz_list_take(SzList *xs, int64_t n);
/* Skip n heads and retain the suffix. n <= 0 retains xs. */
SzList *sz_list_drop(SzList *xs, int64_t n);
/* Last n heads as a shared suffix. n <= 0 is empty. */
SzList *sz_list_take_right(SzList *xs, int64_t n);
/* Drop last n heads as a new prefix spine. n <= 0 retains xs. */
SzList *sz_list_drop_right(SzList *xs, int64_t n);
/* All but the last cell as a new spine. Empty or one cell is empty. */
SzList *sz_list_init(SzList *xs);
/* Last head as a one-cell list. Empty if none. */
SzList *sz_list_last(SzList *xs);
/* Borrowed head at `index`, or `dflt`. Negative / OOB is `dflt`. */
void *sz_list_get_or(SzList *xs, int64_t index, void *dflt);
/* `n` copies of `x`. n <= 0 is empty. Cons retains `x`. Drop owned `x` after. */
SzList *sz_list_fill(int64_t n, void *x);
/* First head for which `pred` is nonzero, as a one-cell list. Empty if none. */
SzList *sz_list_find(SzList *xs, SzListPred pred, void *env);
/* 1 if any head matches `pred`, else 0. */
int64_t sz_list_exists(SzList *xs, SzListPred pred, void *env);
/* Keep a prefix while `pred` is nonzero. New spine. */
SzList *sz_list_takewhile(SzList *xs, SzListPred pred, void *env);
/* Skip a prefix while `pred` is nonzero, then retain the suffix. */
SzList *sz_list_dropwhile(SzList *xs, SzListPred pred, void *env);
/* 1 if every head matches `pred` (empty is 1), else 0. */
int64_t sz_list_forall(SzList *xs, SzListPred pred, void *env);
/* Copy the left spine and share `ys`. Empty `xs` retains `ys`. */
SzList *sz_list_concat(SzList *xs, SzList *ys);
/* Concatenate inner lists. Empty `xss` is empty. */
SzList *sz_list_flatten(SzList *xss);
/* New spine. `fn` returns an owned pointer. Retain `head` when `fn` yields it.
 * Cons retains the mapped head. Map then drops the mapper ref. */
SzList *sz_list_map(SzList *xs, SzListMapFn fn, void *env);
/* Mapper returns a list (+1). Concatenates in order. Empty stays empty. */
SzList *sz_list_flat_map(SzList *xs, SzListMapFn fn, void *env);
/* Pad with `x` until length `n`. n <= len leaves the list. n <= 0 leaves the list. */
SzList *sz_list_pad_to(SzList *xs, int64_t n, void *x);
int sz_list_non_empty(const SzList *xs);
/* Boxed ints `[from, until)`. Empty when `until` <= `from`. Cons retains each box. */
SzList *sz_list_range(int64_t from, int64_t until);
/* `f(0)` … `f(n-1)`. n <= 0 is empty. `fn` sees a boxed index and returns +1. */
SzList *sz_list_tabulate(int64_t n, SzListMapFn fn, void *env);
/* Insert `x` between cells. Empty or one cell shares. Cons retains `x`. */
SzList *sz_list_intersperse(SzList *xs, void *x);
/* Chunks of length `n`. Last chunk may be short. n <= 0 is empty. */
SzList *sz_list_grouped(SzList *xs, int64_t n);
/* Overlapping windows of length `n`. n <= 0 or n > len is empty. */
SzList *sz_list_sliding(SzList *xs, int64_t n);
/* Cells `[from, until)`. Negative `from` / `until` is 0. Empty when until <= from. */
SzList *sz_list_slice(SzList *xs, int64_t from, int64_t until);
/* Boxed ints `[0, len)`. Empty when `xs` is empty. */
SzList *sz_list_indices(SzList *xs);
/* Two lists packed as List[List]: take then drop. */
SzList *sz_list_split_at(SzList *xs, int64_t n);
/* Two lists packed as List[List]: takeWhile then dropWhile. */
SzList *sz_list_span(SzList *xs, SzListPred pred, void *env);
/* Two lists packed as List[List]: filter then filterNot. */
SzList *sz_list_partition(SzList *xs, SzListPred pred, void *env);
/* Prefixes including empty and the full list. */
SzList *sz_list_inits(SzList *xs);
/* Suffixes including the full list and empty. */
SzList *sz_list_tails(SzList *xs);
/* Matching cells as (A, B) pairs. Stops at the shorter list. */
SzList *sz_list_zip(SzList *xs, SzList *ys);
/* x0, y0, x1, y1, … then the leftover tail of the longer list. */
SzList *sz_list_interleave(SzList *xs, SzList *ys);
/* Zip and pad the shorter list with `x` or `y`. */
SzList *sz_list_zip_all(SzList *xs, SzList *ys, void *x, void *y);
/* (List[A], List[B]) from List[(A, B)]. A null pair is skipped.
 * Empty is two empty lists. */
SzPair *sz_list_unzip(SzList *pairs);
/* List[(Int, T)] in order. Empty stays empty. */
SzList *sz_list_zip_with_index(SzList *xs);
/* Fold with `fn((acc, head), env)`. Empty returns a retain of `z`.
 * `fn` returns an owned acc. */
void *sz_list_fold_left(SzList *xs, void *z, SzListMapFn fn, void *env);
/* Fold with `fn((head, acc), env)` from the right. Empty returns a retain of `z`.
 * `fn` returns an owned acc. */
void *sz_list_fold_right(SzList *xs, void *z, SzListMapFn fn, void *env);
/* Accumulators including `z`. `fn((acc, head), env)` like foldLeft.
 * Empty is a one-cell list of `z`. */
SzList *sz_list_scan_left(SzList *xs, void *z, SzListMapFn fn, void *env);
/* Accumulators including `z` from the right. `fn((head, acc), env)`.
 * Empty is a one-cell list of `z`. */
SzList *sz_list_scan_right(SzList *xs, void *z, SzListMapFn fn, void *env);
/* Fold from the first cell. Empty panics. `fn((acc, head), env)`.
 * `fn` returns an owned acc. */
void *sz_list_reduce_left(SzList *xs, SzListMapFn fn, void *env);
/* Fold from the last cell. Empty panics. `fn((head, acc), env)`.
 * `fn` returns an owned acc. */
void *sz_list_reduce_right(SzList *xs, SzListMapFn fn, void *env);
/* Rows to columns. Stops at the shortest row. Empty is empty. */
SzList *sz_list_transpose(SzList *xss);
/* 1 when any cell equals `x` (string / boxed i64 by value). */
int64_t sz_list_contains(SzList *xs, void *x);
/* First index of `x`, or -1. */
int64_t sz_list_index_of(SzList *xs, void *x);
/* Last index of `x`, or -1. */
int64_t sz_list_last_index_of(SzList *xs, void *x);
/* First cell of each equal value. Empty stays empty. */
SzList *sz_list_distinct(SzList *xs);
/* First cell of each key that `fn` returns. `key_kind` is 0 for boxed
 * Int, 1 for String. Empty stays empty. */
SzList *sz_list_distinct_by(SzList *xs, SzListMapFn fn, void *env,
                            int32_t key_kind);
/* Map from `List[(K, V)]`. Duplicate keys keep the last value. A null
 * pair is skipped. Empty is empty. Key kind is boxed Int or String. */
SzMap *sz_list_to_map(SzList *pairs);
/* Set from `List[Int]` or `List[String]`. Duplicate cells collapse.
 * Empty is empty. `key_kind` is 0 for boxed Int, 1 for String. */
SzMap *sz_list_to_set(SzList *xs, int32_t key_kind);
/* Cells of `xs` that are missing from `ys`. Empty `xs` is empty. */
SzList *sz_list_diff(SzList *xs, SzList *ys);
/* Cells of `xs` that occur in `ys`. Empty `xs` or `ys` is empty. */
SzList *sz_list_intersect(SzList *xs, SzList *ys);
/* First index where `pred` is true, or -1. */
int64_t sz_list_index_where(SzList *xs, SzListPred pred, void *env);
/* Last index where `pred` is true, or -1. */
int64_t sz_list_last_index_where(SzList *xs, SzListPred pred, void *env);
/* 1 when `xs` begins with `prefix`. Empty prefix is 1. */
int64_t sz_list_starts_with(SzList *xs, SzList *prefix);
/* 1 when `xs` ends with `suffix`. Empty suffix is 1. */
int64_t sz_list_ends_with(SzList *xs, SzList *suffix);
/* 1 when `xs` and `ys` have the same cells in order. */
int64_t sz_list_same_elements(SzList *xs, SzList *ys);
/* Replace `replaced` cells from `from` with `other`. Negative is 0.
 * `from` past the end appends `other`. */
SzList *sz_list_patch(SzList *xs, int64_t from, SzList *other, int64_t replaced);
/* Last head for which `pred` is nonzero, as a one-cell list. Empty if none. */
SzList *sz_list_find_last(SzList *xs, SzListPred pred, void *env);
/* Length of the leading prefix where `pred` is nonzero. */
int64_t sz_list_prefix_length(SzList *xs, SzListPred pred, void *env);
/* First index of `slice`, or -1. Empty slice is 0. */
int64_t sz_list_index_of_slice(SzList *xs, SzList *slice);
/* Last index of `slice`, or -1. Empty slice is len. */
int64_t sz_list_last_index_of_slice(SzList *xs, SzList *slice);
/* prefixLength of the suffix that starts at `from`. Negative `from` is 0. */
int64_t sz_list_segment_length(SzList *xs, SzListPred pred, void *env, int64_t from);
/* 1 when `index` is a valid cell index. */
int64_t sz_list_is_defined_at(SzList *xs, int64_t index);
/* Negative when len < n, 0 when equal, positive when len > n. */
int64_t sz_list_length_compare(SzList *xs, int64_t n);
/* `as_int` 1 orders boxed Int, else String. `want_max` 1 is max. Empty max panics. */
SzList *sz_list_sort(SzList *xs, int64_t as_int);
SzList *sz_list_sort_by(SzList *xs, SzListMapFn fn, void *env);
void *sz_list_max(SzList *xs, int64_t as_int);
void *sz_list_min(SzList *xs, int64_t as_int);
void *sz_list_max_by(SzList *xs, SzListMapFn fn, void *env, int64_t want_max);
/* Group cells by the `Int` or `String` key that `fn` returns. `key_kind`
 * is 0 for boxed Int, 1 for String. Empty is empty. Cells in a group
 * keep their order. */
SzMap *sz_list_group_by(SzList *xs, SzListMapFn fn, void *env, int32_t key_kind);
/* Sum boxed Int cells. Empty is 0. */
int64_t sz_list_sum(SzList *xs);
/* Product of boxed Int cells. Empty is 1. */
int64_t sz_list_product(SzList *xs);
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
/* 0–1 list of the value. Miss is empty. Cons retains the value. */
SzList *sz_map_get(SzMap *m, void *key);
int64_t sz_map_contains(SzMap *m, void *key);
/* Drop `key` if present. Missing key retains `m`. */
SzMap *sz_map_remove(SzMap *m, void *key);
/* Inorder keys as a new list. Cons retains keys. */
SzList *sz_map_keys(SzMap *m);
/* Inorder values as a new list. Cons retains values. */
SzList *sz_map_values(SzMap *m);
/* Inorder `(K, V)` pairs as a new list. Empty is empty. */
SzList *sz_map_to_list(SzMap *m);
int64_t sz_map_size(SzMap *m);
/* Set algebra. Values stay null. Empty `b` retains `a` for union/diff. */
SzMap *sz_set_union(SzMap *a, SzMap *b);
SzMap *sz_set_intersect(SzMap *a, SzMap *b);
SzMap *sz_set_diff(SzMap *a, SzMap *b);
/* Map algebra. Keep values. Union takes values from `b`. Intersect and
 * diff take values from `a`. Empty `b` retains `a` for union/diff. */
SzMap *sz_map_union(SzMap *a, SzMap *b);
SzMap *sz_map_intersect(SzMap *a, SzMap *b);
SzMap *sz_map_diff(SzMap *a, SzMap *b);
/* Keep entries whose value matches `pred`. Empty stays empty. */
SzMap *sz_map_filter(SzMap *m, SzListPred pred, void *env);
/* Map each value. Mapper returns +1. Empty stays empty. */
SzMap *sz_map_map_values(SzMap *m, SzListMapFn fn, void *env);
/* 1 when any value matches `pred`. Empty is 0. */
int64_t sz_map_exists(SzMap *m, SzListPred pred, void *env);
/* 1 when every value matches `pred`. Empty is 1. */
int64_t sz_map_forall(SzMap *m, SzListPred pred, void *env);
/* Keep keys that match `pred`. Empty stays empty. */
SzMap *sz_set_filter(SzMap *s, SzListPred pred, void *env);
/* Map each key. Mapper returns +1. `key_kind` is 0 for boxed Int, 1 for
 * String. Duplicate keys collapse. Empty stays empty. */
SzMap *sz_set_map(SzMap *s, SzListMapFn fn, void *env, int32_t key_kind);
/* 1 when any key matches `pred`. Empty is 0. */
int64_t sz_set_exists(SzMap *s, SzListPred pred, void *env);
/* 1 when every key matches `pred`. Empty is 1. */
int64_t sz_set_forall(SzMap *s, SzListPred pred, void *env);
/* 1 when every key of `a` is in `b`. Empty `a` is a subset. */
int64_t sz_set_is_subset(SzMap *a, SzMap *b);
/* 1 when `a` and `b` share no key. Empty is disjoint. */
int64_t sz_set_is_disjoint(SzMap *a, SzMap *b);

/* Blessed filesystem IO (live or TestRuntime mem FS; chosen when the IO runs) */
SzIo *sz_fs_read(SzString *path);
SzIo *sz_fs_write(SzString *path, SzString *contents);
SzIo *sz_fs_list(SzString *path);
SzIo *sz_fs_exists(SzString *path);
SzIo *sz_fs_delete(SzString *path);
SzIo *sz_fs_rename(SzString *from, SzString *to);
SzIo *sz_fs_walk(SzString *path);
SzIo *sz_fs_mkdirs(SzString *path);
SzIo *sz_fs_canonicalize(SzString *path);
/* Pure path helpers (follow Str.concat). */
SzString *sz_fs_join(SzString *a, SzString *b);
SzString *sz_fs_dirname(SzString *path);
SzString *sz_fs_basename(SzString *path);

/* Pure Json. Enum Null|Bool|Int|Float|Str|Arr|Obj.
 * parse / stringify return Result (Err=0 / Ok=1). Query kits return empty
 * lists on a miss or a wrong tag. `*_or` / `get_*` use the default.
 * Write kits copy Obj / Arr cells. A miss keeps the default or retains `j`. */
SzAdt *sz_json_parse(SzString *s);
SzAdt *sz_json_stringify(SzAdt *j);
SzList *sz_json_get(SzAdt *j, SzString *key); /* List[Json]; empty miss */
SzList *sz_json_keys(SzAdt *j);               /* List[String]; empty if not Obj */
SzList *sz_json_arr(SzAdt *j);                /* List[Json]; empty if not Arr */
SzList *sz_json_at(SzAdt *j, int64_t i);      /* List[Json]; empty if out of range */
int64_t sz_json_has(SzAdt *j, SzString *key); /* 1 when get is non-empty */
SzList *sz_json_pairs(SzAdt *j);              /* List[(String, Json)]; empty if not Obj */
int64_t sz_json_is_null(SzAdt *j);
int64_t sz_json_is_bool(SzAdt *j);
int64_t sz_json_is_int(SzAdt *j);
int64_t sz_json_is_str(SzAdt *j);
int64_t sz_json_is_arr(SzAdt *j);
int64_t sz_json_is_obj(SzAdt *j);
SzList *sz_json_as_bool(SzAdt *j); /* List[Bool]; empty if not Bool */
SzList *sz_json_as_int(SzAdt *j);  /* List[Int]; empty if not Int */
SzList *sz_json_as_float(SzAdt *j); /* List[Float]; empty if not Float */
SzList *sz_json_as_str(SzAdt *j);  /* List[String]; empty if not Str */
int64_t sz_json_bool_or(SzAdt *j, int64_t d);
int64_t sz_json_int_or(SzAdt *j, int64_t d);
SzString *sz_json_str_or(SzAdt *j, SzString *d);
int64_t sz_json_get_bool(SzAdt *j, SzString *key, int64_t d);
int64_t sz_json_get_int(SzAdt *j, SzString *key, int64_t d);
SzString *sz_json_get_str(SzAdt *j, SzString *key, SzString *d);
double sz_json_float_or(SzAdt *j, double d);
double sz_json_get_float(SzAdt *j, SzString *key, double d);
/* Obj merge. Right wins on a duplicate key. Non-obj arguments stay the
 * other obj, or `b` when neither is Obj. */
SzAdt *sz_json_merge(SzAdt *a, SzAdt *b);
/* Write kits. `set` / `remove` copy an Obj. A non-Obj `set` becomes a
 * one-key Obj. `remove` on a non-Obj retains `j`. `append` / `prepend` copy
 * an Arr; a non-Arr becomes a one-cell Arr. `setAt` / `dropAt` retain `j`
 * when the index is out of range or `j` is not Arr. */
SzAdt *sz_json_set(SzAdt *j, SzString *key, SzAdt *v);
SzAdt *sz_json_remove(SzAdt *j, SzString *key);
SzAdt *sz_json_append(SzAdt *j, SzAdt *v);
SzAdt *sz_json_prepend(SzAdt *j, SzAdt *v);
SzAdt *sz_json_set_at(SzAdt *j, int64_t i, SzAdt *v);
SzAdt *sz_json_drop_at(SzAdt *j, int64_t i);

/* Process / args / env / console (console out = IO.println) */
void sz_sys_set_args(int argc, char **argv);
SzIo *sz_sys_args(void);
SzIo *sz_sys_read_line(void); /* IO[String]: one stdin line; EOF → ""; parks on poll */
SzIo *sz_sys_read(int64_t n); /* IO[String]: n stdin bytes (or fewer at EOF); parks on poll */
SzIo *sz_sys_write(SzString *s); /* IO[Unit]: stdout bytes, no newline */
SzIo *sz_sys_exec(SzString *cmd); /* IO[(Int, String, String)] code+stdout+stderr; parks on poll; fails under TestRuntime */
SzIo *sz_sys_spawn(SzString *cmd); /* IO[Int] pid; stdin/stdout pipes; fails under TestRuntime */
SzIo *sz_sys_child_write(int64_t pid, SzString *s); /* IO[Unit] child stdin; parks on poll */
SzIo *sz_sys_child_read(int64_t pid, int64_t n); /* IO[String] child stdout bytes; fewer at EOF; parks on poll */
SzIo *sz_sys_child_close(int64_t pid); /* IO[Unit] close child stdin */
SzIo *sz_sys_alive(int64_t pid);   /* IO[Int] 1 if running */
SzIo *sz_sys_kill(int64_t pid);    /* IO[Unit] SIGTERM; no-op if already gone */
SzIo *sz_sys_getenv(SzString *key);

/* Blessed Clock / Random / Net (impurity boundary) */
SzIo *sz_clock_real_time(void);   /* IO[Int] wall epoch ms */
SzIo *sz_clock_monotonic(void);   /* IO[Int] monotonic ms */
int64_t sz_clock_monotonic_ms_sync(void); /* sync read for UI pump dt */

SzIo *sz_random_next_int(int64_t bound); /* IO[Int] in [0, bound) */

SzIo *sz_net_http_get(SzString *url); /* IO[String] body; 2xx; 1 MiB; http:// or https:// */
SzIo *sz_net_http_post(SzString *url, SzString *body);
SzIo *sz_net_http_put(SzString *url, SzString *body);
SzIo *sz_net_http_patch(SzString *url, SzString *body);
SzIo *sz_net_http_delete(SzString *url);
SzIo *sz_net_http_head(SzString *url);
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env); /* IO[Unit]; one request; handler gets (path, method, body) */
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env); /* IO[Unit]; keep listen; drop bad clients/handlers */
typedef struct SzNetSock {
  int fd;
  int fd6;
  int kind; /* 1 tcp, 2 listen, 3 udp */
  int fake_id;
  int64_t port;
} SzNetSock;
/* Blessed TCP. Listen is localhost. Connect takes IPv4/IPv6 literals or localhost. */
SzIo *sz_net_tcp_connect(SzString *host, int64_t port); /* IO[SzNetSock] */
SzIo *sz_net_tcp_listen(int64_t port);                  /* IO[SzNetSock] */
SzIo *sz_net_tcp_accept(SzNetSock *listener);           /* IO[SzNetSock] */
SzIo *sz_net_tcp_read(SzNetSock *conn, int64_t n);      /* IO[String] */
SzIo *sz_net_tcp_write(SzNetSock *conn, SzString *s);   /* IO[Unit] */
SzIo *sz_net_tcp_close(SzNetSock *sock);                /* IO[Unit] */
/* Blessed UDP. Bind 0 picks an ephemeral port. Recv is (host, port, data). */
SzIo *sz_net_udp_bind(int64_t port); /* IO[SzNetSock] */
SzIo *sz_net_udp_send(SzNetSock *sock, SzString *host, int64_t port,
                     SzString *data);
SzIo *sz_net_udp_recv(SzNetSock *sock, int64_t n); /* IO[(String, Int, String)] */
SzIo *sz_net_udp_close(SzNetSock *sock);
void sz_net_sock_on_free(SzNetSock *s);
void sz_testrt_net_sock_gone(SzNetSock *s);
/* Test-only: UDP nameserver for live HTTP DNS. NULL ip restores /etc/resolv.conf. */
void sz_net_test_set_nameserver(const char *ipv4, int port);
/* Test-only: Host header value for HTTP (RFC 9110). */
void sz_net_test_http_host_header(const char *host, int port, char *out,
                                 size_t cap);

/* TestRuntime — fake interpreters for deterministic scuzz test / fuzz */
void sz_testrt_install(void); /* fake clock+rng+mem FS+stub net+sys/console */
void sz_testrt_reset(void);   /* restore live interpreters */

/* PCT schedule (fiber run). SCUZZ_SCHED_SEED arms priority + change-points.
 * Packed seed: k=s%8, d=2+(s/8)%4, rng=s/32. SCUZZ_PCT_D / SCUZZ_PCT_K override. */

/* Fault injection (TestRuntime). Kind ids match SCUZZ_FAULT_KIND. */
enum { SZ_FAULT_FS = 1, SZ_FAULT_NET = 2, SZ_FAULT_QUEUE = 3 };
enum { SZ_FAULT_FAIL = 0, SZ_FAULT_DROP = 1, SZ_FAULT_CORRUPT = 2 };
/* 1 when this op is the armed Nth op of `kind`. */
int sz_testrt_fault_tick(int kind);
/* Observation log: one line. Forwards to the interned timeline log. */
void sz_effect_log(const char *line);
/* Delay thunks stash a fail message; the DELAY step consumes it. */
void sz_testrt_fault_note(const char *msg);
const char *sz_testrt_fault_take_msg(void);
/* Implicit oracles under SCUZZ_TESTRT=1. */
int sz_testrt_oracles_armed(void);
void sz_testrt_ui_idle_snapshot(void);
void sz_testrt_ui_idle_check(void);
void sz_testrt_ui_idle_reset(void);
/* Heap baseline: live bytes/counts and RC sum vs a snapshot. Growth or a
 * leftover retain fails. Process snapshot is for C tests. Session snapshot
 * is after mount, before the workload, and is the UI terminal check. */
void sz_testrt_heap_baseline_snapshot(void);
void sz_testrt_heap_baseline_check(void);
void sz_testrt_session_baseline_snapshot(void);
void sz_testrt_session_baseline_check(void);
/* Planted-fail hooks for C tests. Skip orphan cancel or unstepped ensure. */
void sz_testrt_plant_skip_orphan_cancel(void);
void sz_testrt_plant_skip_unstepped_ensure(void);
int sz_testrt_skip_orphan_cancel(void);
int sz_testrt_skip_unstepped_ensure(void);

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
SzIo *sz_testrt_fs_exists(SzString *path);
SzIo *sz_testrt_fs_delete(SzString *path);
SzIo *sz_testrt_fs_rename(SzString *from, SzString *to);
SzIo *sz_testrt_fs_walk(SzString *path);
SzIo *sz_testrt_fs_mkdirs(SzString *path);
SzIo *sz_testrt_fs_canonicalize(SzString *path);

void sz_testrt_net_stub(const char *url, const char *body);
void sz_testrt_net_inject_request(const char *path); /* GET path, empty body */
void sz_testrt_net_queue_request(const char *path);
void sz_testrt_net_inject_http(const char *method, const char *path,
                              const char *body);
void sz_testrt_net_queue_http(const char *method, const char *path,
                             const char *body);
int sz_testrt_net_serve_pending(void);
int sz_testrt_net_serve_pending_port(int64_t port); /* injects plus mailbox items */
char *sz_testrt_net_pop_request(void); /* owned; NULL if empty */
int sz_testrt_net_is_fake(void);
SzIo *sz_testrt_net_http_req(const char *method, SzString *url, SzString *body);
SzIo *sz_testrt_net_accept(int64_t port); /* IO[(req, Deferred|null)] */
SzIo *sz_testrt_net_tcp_connect(SzString *host, int64_t port);
SzIo *sz_testrt_net_tcp_listen(int64_t port);
SzIo *sz_testrt_net_tcp_accept(SzNetSock *listener);
SzIo *sz_testrt_net_tcp_read(SzNetSock *conn, int64_t n);
SzIo *sz_testrt_net_tcp_write(SzNetSock *conn, SzString *s);
SzIo *sz_testrt_net_tcp_close(SzNetSock *sock);
SzIo *sz_testrt_net_udp_bind(int64_t port);
SzIo *sz_testrt_net_udp_send(SzNetSock *sock, SzString *host, int64_t port,
                            SzString *data);
SzIo *sz_testrt_net_udp_recv(SzNetSock *sock, int64_t n);
SzIo *sz_testrt_net_udp_close(SzNetSock *sock);
void sz_testrt_net_cancel_accept(int64_t port);
void sz_testrt_net_fail_mailbox(int64_t port, SzError *err);
/* 1 when `url` is http loopback (`127.0.0.1` / `localhost` / `::1`). */
int sz_testrt_net_parse_loopback(const char *url, int64_t *port, char *path,
                                size_t path_cap);
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

/* Properties — residual checks armed only under SCUZZ_TESTRT=1 */
void sz_property_stash_a11y(const char *dump);
int64_t sz_property_a11y_has(SzString *needle);
SzIo *sz_property_assert(SzString *name, int64_t ok);
void sz_property_check(SzString *name, int64_t ok);
void sz_property_sometimes(SzString *name);
void sz_property_sometimes_flush(void);
void sz_timeline_varied_flush(void);
void sz_property_classify(SzString *name, int64_t hit);
void sz_property_classify_flush(void);
void sz_property_always_register(SzString *name, void *fn);
void sz_property_eventually_register(SzString *name, void *fn);
void sz_property_response_register(SzString *name, void *trigger_fn,
                                   void *response_fn);
/* Session claim verdict: a Timeline => Verdict claim judges the frozen
 * timeline at session end. `valid` 1 = ok; `index` is the failing state
 * (-1 = none); `why` is a static or author-owned string (never freed). */
typedef struct SzVerdict {
  int64_t valid;
  int64_t index;
  const char *why;
} SzVerdict;

/* Shared static ok verdict; no alloc. */
SzVerdict *sz_verdict_ok(void);
/* Host-malloc (not sz_alloc): judge-side memory must not perturb app heap
 * accounting. Stores `why`; an invalid verdict ends in a claim panic, so it
 * is never freed. */
SzVerdict *sz_verdict_fail(int64_t index, const char *why);
/* First invalid wins; both valid -> ok. */
SzVerdict *sz_verdict_and(SzVerdict *a, SzVerdict *b);
/* First valid wins. */
SzVerdict *sz_verdict_or(SzVerdict *a, SzVerdict *b);
/* Same lambda calling convention as sz_timeline_forall. */
SzVerdict *sz_verdict_every(void *tl, void *fnp, void *envp);
SzVerdict *sz_verdict_any(void *tl, void *fnp, void *envp);
void sz_verify_register(const char *name, SzVerdict *(*fn)(void *));
/* Relation claim: (Timeline, Timeline) => Verdict over a pair of dumps. */
void sz_verify_register_rel(const char *name, SzVerdict *(*fn)(void *, void *));
/* Parse a v1 or v2 timeline dump (SCUZZ_TIMELINE_DUMP output). NULL on error.
 * Writer emits v=2 (effects / fibers / fault). v=1 loads with those empty. */
void *sz_timeline_load(const char *path);
void sz_timeline_free(void *tl);
/* Record a blessed kit op. Never stores a payload: size plus FNV-1a hash. */
void sz_timeline_log_bytes(const char *op, const void *p, size_t n);
void sz_timeline_log_cstr(const char *op, const char *s);
/* Judge mode: spec is "<a.txt>,<b.txt>"; runs every registered relation
 * claim over the pair. Process exit code: 0 = all valid (or none registered),
 * 1 = a claim failed, 2 = bad spec or unreadable dump. */
int sz_judge_rel_main(const char *spec);
void sz_property_stash_last_hit(const char *desc);
int64_t sz_property_last_hit_has(SzString *needle);
int sz_property_session_armed(void);
void sz_property_session_step(void);
void sz_property_session_end(void);
void sz_property_session_reset(void);
void sz_timeline_set_drive(const char *line);
int sz_timeline_replaying(void);
int64_t sz_timeline_replay_signal_int(int64_t id);
int64_t sz_timeline_len(void *tl);
int64_t sz_timeline_signal_int(void *tl, int64_t i, int64_t id);
int64_t sz_timeline_signal_list_len(void *tl, int64_t i, int64_t id);
int64_t sz_timeline_signal_str_has(void *tl, int64_t i, int64_t id,
                                  SzString *needle);
int64_t sz_timeline_a11y_has(void *tl, int64_t i, SzString *needle);
int64_t sz_timeline_last_hit_has(void *tl, int64_t i, SzString *needle);
int64_t sz_timeline_drive_has(void *tl, int64_t i, SzString *needle);
int64_t sz_timeline_effect_has(void *tl, int64_t i, SzString *needle);
int64_t sz_timeline_effect_count(void *tl, int64_t i);
int64_t sz_timeline_fiber_live(void *tl, int64_t i);
int64_t sz_timeline_fiber_ready(void *tl, int64_t i);
int64_t sz_timeline_fiber_parked(void *tl, int64_t i);
int64_t sz_timeline_fiber_done(void *tl, int64_t i);
int64_t sz_timeline_fault_kind_has(void *tl, int64_t i, SzString *needle);
int64_t sz_timeline_fault_n(void *tl, int64_t i);
int64_t sz_timeline_checkpoint(void *tl, int64_t i); /* 1 when state i is a checkpoint */
/* Nearest checkpoint index at or before i, else the next checkpoint, else -1. */
int64_t sz_timeline_nearest_checkpoint(void *tl, int64_t i);
/* Drop non-checkpoint states from the live recording. */
void sz_timeline_compact(void);
/* Drop non-checkpoint states from a loaded timeline. */
void sz_timeline_compact_loaded(void *tl);
/* Restore observation from the nearest checkpoint at or before i. */
void sz_timeline_replay_from(int64_t i);
int64_t sz_timeline_forall(void *tl, SzListPred pred, void *env);
int64_t sz_timeline_exists(void *tl, SzListPred pred, void *env);
int sz_drive_uncons(const char *tok, const char *name, char *inner, int cap);
int sz_drive_uncons_list(const char *tok, char *inner, int cap);
int64_t sz_drive_nfields(const char *inner);
int sz_drive_field(const char *inner, int64_t i, char *out, int cap);
int64_t sz_drive_parse_int(const char *tok);
int64_t sz_drive_parse_bool(const char *tok);
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
