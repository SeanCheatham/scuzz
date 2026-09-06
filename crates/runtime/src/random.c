#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stdio.h>
#include <time.h>

/* Blessed Random. Live seeds once from /dev/urandom. TestRuntime seeds from
 * a caller value. Both paths run xoshiro256**. Bounded ints use Lemire 2019
 * multiply-shift with rejection, so every value in [0, bound) is uniform. */

static int g_fake = 0;
static uint64_t g_state[4];

/* Splitmix64 expands one 64-bit seed into the xoshiro state. Any seed is
 * valid, including 0. The expanded state is never all-zero. */
static uint64_t splitmix64(uint64_t *x) {
  uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

static void seed_state(uint64_t s[4], uint64_t seed) {
  uint64_t x = seed;
  int i;
  for (i = 0; i < 4; i++)
    s[i] = splitmix64(&x);
}

void sz_testrt_random_install(uint64_t seed) {
  g_fake = 1;
  seed_state(g_state, seed);
}

void sz_testrt_random_reset_live(void) { g_fake = 0; }

static uint64_t live_seed(void) {
  FILE *f = fopen("/dev/urandom", "rb");
  uint64_t s = 0;
  if (f) {
    if (fread(&s, sizeof(s), 1, f) != 1)
      s = 0;
    fclose(f);
  }
  if (s == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    s = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ 0x9e3779b97f4a7c15ULL;
  }
  return s;
}

static uint64_t rotl64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t xoshiro_next(uint64_t s[4]) {
  uint64_t r = rotl64(s[1] * 5, 7) * 9;
  uint64_t t = s[1] << 17;
  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];
  s[2] ^= t;
  s[3] = rotl64(s[3], 45);
  return r;
}

static uint64_t next_u64(void) {
  if (!g_fake) {
    /* Live state stays separate: a TestRuntime install must not perturb the
     * live stream. Seed once from /dev/urandom (realtime mix fallback). */
    static int inited = 0;
    static uint64_t live[4];
    if (!inited) {
      seed_state(live, live_seed());
      inited = 1;
    }
    return xoshiro_next(live);
  }
  return xoshiro_next(g_state);
}

static void *random_next_thunk(void *env) {
  int64_t bound = sz_unbox_i64(env);
  uint64_t lim = (uint64_t)bound;
  uint64_t u;
  uint64_t lo;
  __uint128_t m;
  sz_timeline_log_cstr("Random.nextInt", "");
  /* Lemire 2019 with rejection: uniform in [0, lim), no modulo bias. */
  u = next_u64();
  m = (__uint128_t)u * lim;
  lo = (uint64_t)m;
  if (lo < lim) {
    uint64_t t = (uint64_t)(0 - lim) % lim; /* 2^64 mod lim */
    while (lo < t) {
      u = next_u64();
      m = (__uint128_t)u * lim;
      lo = (uint64_t)m;
    }
  }
  return sz_box_i64((int64_t)(m >> 64));
}

SzIo *sz_random_next_int(int64_t bound) {
  void *b;
  SzIo *io;
  if (bound <= 0)
    return sz_io_fail_cstr("Random.nextInt: bound <= 0");
  b = sz_box_i64(bound);
  io = sz_io_delay(random_next_thunk, b);
  sz_release(b);
  return io;
}
