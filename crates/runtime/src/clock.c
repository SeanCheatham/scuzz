#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <time.h>

/* Live vs fake clock. Fake: virtual ms advanced by sleep / sz_testrt_clock_advance. */

static int g_fake = 0;
static int64_t g_now_ms = 0;

void sz_testrt_clock_install(int64_t start_ms) {
  g_fake = 1;
  g_now_ms = start_ms < 0 ? 0 : start_ms;
}

void sz_testrt_clock_advance(int64_t ms) {
  if (!g_fake)
    return;
  if (ms <= 0)
    return;
  /* Saturate. Signed add of a large sleep is undefined. */
  if (g_now_ms > INT64_MAX - ms)
    g_now_ms = INT64_MAX;
  else
    g_now_ms += ms;
}

int sz_testrt_clock_is_fake(void) { return g_fake; }

int64_t sz_testrt_clock_now_ms(void) { return g_now_ms; }

void sz_testrt_clock_reset_live(void) { g_fake = 0; }

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

int64_t sz_clock_monotonic_ms_sync(void) {
  return g_fake ? g_now_ms : live_monotonic_ms();
}

static void *clock_real_thunk(void *env) {
  (void)env;
  sz_timeline_log_cstr("Clock.realTime", "");
  int64_t ms = g_fake ? g_now_ms : live_realtime_ms();
  return sz_box_i64(ms);
}

static void *clock_mono_thunk(void *env) {
  (void)env;
  sz_timeline_log_cstr("Clock.monotonic", "");
  int64_t ms = g_fake ? g_now_ms : live_monotonic_ms();
  return sz_box_i64(ms);
}

SzIo *sz_clock_real_time(void) { return sz_io_delay(clock_real_thunk, NULL); }

SzIo *sz_clock_monotonic(void) { return sz_io_delay(clock_mono_thunk, NULL); }

/* UTC ISO-8601 from epoch milliseconds. Millisecond precision. Z suffix.
 * Proleptic Gregorian. No leap seconds. No time zone.
 * Years 0 through 9999 use four digits. Other years use the full signed year.
 * Civil conversion is Howard Hinnant days_from_civil inverted. */

static int64_t floor_div(int64_t a, int64_t b) {
  int64_t q = a / b;
  int64_t r = a % b;
  if (r < 0)
    return q - 1;
  return q;
}

static int64_t floor_mod(int64_t a, int64_t b) {
  int64_t r = a % b;
  if (r < 0)
    return r + b;
  return r;
}

static void civil_from_days(int64_t z, int64_t *y_out, int *m_out, int *d_out) {
  int64_t era;
  unsigned doe;
  unsigned yoe;
  int64_t y;
  unsigned doy;
  unsigned mp;
  unsigned d;
  unsigned m;

  z += 719468;
  era = (z >= 0 ? z : z - 146096) / 146097;
  doe = (unsigned)(z - era * 146097);
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int64_t)yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += (m <= 2);
  *y_out = y;
  *m_out = (int)m;
  *d_out = (int)d;
}

SzString *sz_clock_iso8601(int64_t ms) {
  int64_t sec;
  int64_t days;
  int64_t y;
  int milli;
  int sod;
  int hour;
  int min;
  int s;
  int month;
  int day;
  char buf[64];
  int n;

  sec = floor_div(ms, 1000);
  milli = (int)floor_mod(ms, 1000);
  days = floor_div(sec, 86400);
  sod = (int)floor_mod(sec, 86400);
  hour = sod / 3600;
  min = (sod % 3600) / 60;
  s = sod % 60;
  civil_from_days(days, &y, &month, &day);
  if (y >= 0 && y <= 9999)
    n = snprintf(buf, sizeof buf, "%04" PRId64 "-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 y, month, day, hour, min, s, milli);
  else
    n = snprintf(buf, sizeof buf, "%" PRId64 "-%02d-%02dT%02d:%02d:%02d.%03dZ", y,
                 month, day, hour, min, s, milli);
  if (n < 0 || (size_t)n >= sizeof buf)
    return sz_string_from_cstr("");
  return sz_string_from_cstr(buf);
}
