#define _POSIX_C_SOURCE 200809L
#include "scalui_rt.h"

#include <time.h>

/* Live vs fake clock. Fake: virtual ms advanced by sleep / su_testrt_clock_advance. */

static int g_fake = 0;
static int64_t g_now_ms = 0;

void su_testrt_clock_install(int64_t start_ms) {
  g_fake = 1;
  g_now_ms = start_ms < 0 ? 0 : start_ms;
}

void su_testrt_clock_advance(int64_t ms) {
  if (!g_fake)
    return;
  if (ms > 0)
    g_now_ms += ms;
}

int su_testrt_clock_is_fake(void) { return g_fake; }

int64_t su_testrt_clock_now_ms(void) { return g_now_ms; }

void su_testrt_clock_reset_live(void) { g_fake = 0; }

static int64_t live_realtime_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    return 0;
  return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

static int64_t live_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

int64_t su_clock_monotonic_ms_sync(void) {
  return g_fake ? g_now_ms : live_monotonic_ms();
}

void su_clock_sleep_ms(int64_t ms) {
  if (ms <= 0)
    return;
  if (g_fake) {
    g_now_ms += ms;
    return;
  }
  {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000L);
    nanosleep(&ts, NULL);
  }
}

static void *clock_real_thunk(void *env) {
  (void)env;
  int64_t ms = g_fake ? g_now_ms : live_realtime_ms();
  return su_box_i64(ms);
}

static void *clock_mono_thunk(void *env) {
  (void)env;
  int64_t ms = g_fake ? g_now_ms : live_monotonic_ms();
  return su_box_i64(ms);
}

SuIo *su_clock_real_time(void) { return su_io_delay(clock_real_thunk, NULL); }

SuIo *su_clock_monotonic(void) { return su_io_delay(clock_mono_thunk, NULL); }
