#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stdio.h>
#include <time.h>

/* Blessed Random — live entropy or TestRuntime seeded LCG. */

static int g_fake = 0;
static uint64_t g_state = 1;

void sz_testrt_random_install(uint64_t seed) {
  g_fake = 1;
  g_state = seed ? seed : 1;
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
  return s ? s : 1;
}

static uint64_t next_u64(void) {
  if (!g_fake) {
    /* Per-call mix from urandom when live; keep a process LCG warm-start. */
    static int inited = 0;
    static uint64_t live = 1;
    if (!inited) {
      live = live_seed();
      inited = 1;
    }
    live = live * 6364136223846793005ULL + 1;
    return live;
  }
  g_state = g_state * 6364136223846793005ULL + 1;
  return g_state;
}

static void *random_next_thunk(void *env) {
  int64_t bound = sz_unbox_i64(env);
  int64_t n;
  if (bound <= 0)
    n = 0;
  else {
    uint64_t u = next_u64() >> 33;
    n = (int64_t)(u % (uint64_t)bound);
  }
  return sz_box_i64(n);
}

SzIo *sz_random_next_int(int64_t bound) {
  void *b = sz_box_i64(bound);
  SzIo *io = sz_io_delay(random_next_thunk, b);
  sz_release(b);
  return io;
}
