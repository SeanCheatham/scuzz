#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
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

static int http_digits(const char *s, size_t n) {
  int value = 0;
  size_t i;
  for (i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9')
      return -1;
    value = value * 10 + s[i] - '0';
  }
  return value;
}

static int http_name(const char *s, size_t n, const char *const *names,
                     int count) {
  int i;
  for (i = 0; i < count; i++)
    if (strlen(names[i]) == n && memcmp(s, names[i], n) == 0)
      return i;
  return -1;
}

static int64_t http_days(int64_t year, int month, int day) {
  int64_t era, yoe, doy;
  year -= month <= 2;
  era = floor_div(year, 400);
  yoe = year - era * 400;
  doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

static int http_date_ms(const char *s, size_t n, int64_t now, int64_t *out) {
  static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  static const char *const short_days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *const long_days[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                        "Thursday", "Friday", "Saturday"};
  static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const char *time;
  int year, month, day, weekday, hour, minute, second, limit, short_year = 0;
  int64_t days;
  if (n == 29 && s[3] == ',' && s[4] == ' ' && s[7] == ' ' &&
      s[11] == ' ' && s[16] == ' ' && memcmp(s + 25, " GMT", 4) == 0) {
    weekday = http_name(s, 3, short_days, 7);
    day = http_digits(s + 5, 2);
    month = http_name(s + 8, 3, months, 12) + 1;
    year = http_digits(s + 12, 4);
    time = s + 17;
  } else if (n == 24 && s[3] == ' ' && s[7] == ' ' && s[10] == ' ' && s[19] == ' ') {
    weekday = http_name(s, 3, short_days, 7);
    month = http_name(s + 4, 3, months, 12) + 1;
    day = s[8] == ' ' ? http_digits(s + 9, 1) : http_digits(s + 8, 2);
    year = http_digits(s + 20, 4);
    time = s + 11;
  } else {
    const char *comma = memchr(s, ',', n);
    size_t name_len;
    if (!comma)
      return 0;
    name_len = (size_t)(comma - s);
    if (n != name_len + 24 || comma[1] != ' ')
      return 0;
    weekday = http_name(s, name_len, long_days, 7);
    s = comma + 2;
    if (s[2] != '-' || s[6] != '-' || s[9] != ' ' || memcmp(s + 18, " GMT", 4) != 0)
      return 0;
    day = http_digits(s, 2);
    month = http_name(s + 3, 3, months, 12) + 1;
    year = http_digits(s + 7, 2);
    time = s + 10;
    short_year = 1;
  }
  if (time[2] != ':' || time[5] != ':')
    return 0;
  hour = http_digits(time, 2);
  minute = http_digits(time + 3, 2);
  second = http_digits(time + 6, 2);
  if (year < 0 || month < 1 || day < 1 || weekday < 0 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 60 ||
      (second == 60 && (hour != 23 || minute != 59)))
    return 0;
  if (short_year) {
    int64_t current_year, boundary;
    int current_month, current_day;
    civil_from_days(floor_div(now, 86400000), &current_year, &current_month, &current_day);
    if (current_year < 1 || current_year > 9999)
      return 0;
    year += (int)((current_year + 50) / 100) * 100;
    boundary = http_days(current_year + 50, current_month, current_day) * 86400000 +
               floor_mod(now, 86400000);
    if (http_days(year, month, day) * 86400000 +
        (hour * 3600 + minute * 60 + second) * 1000 > boundary)
      year -= 100;
  }
  if (year < 1 || year > 9999)
    return 0;
  limit = month_days[month - 1];
  if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    limit++;
  if (day > limit)
    return 0;
  days = http_days(year, month, day);
  if (floor_mod(days + 4, 7) != weekday)
    return 0;
  *out = days * 86400000 + (hour * 3600 + minute * 60 + second) * 1000;
  return 1;
}

int64_t sz_net_retry_after_millis(SzString *value, int64_t now_ms) {
  const char *s;
  size_t n, i;
  int64_t seconds = 0, target;
  if (!value)
    return -1;
  s = sz_string_cstr(value);
  n = (size_t)sz_string_len(value);
  while (n && (*s == ' ' || *s == '\t')) {
    s++;
    n--;
  }
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
    n--;
  if (!n)
    return -1;
  if (*s >= '0' && *s <= '9') {
    for (i = 0; i < n; i++) {
      int digit = s[i] - '0';
      if (digit < 0 || digit > 9 || seconds > (INT64_MAX / 1000 - digit) / 10)
        return -1;
      seconds = seconds * 10 + digit;
    }
    return seconds * 1000;
  }
  if (!http_date_ms(s, n, now_ms, &target))
    return -1;
  if (target <= now_ms)
    return 0;
  if (now_ms < 0 && target > INT64_MAX + now_ms)
    return -1;
  return target - now_ms;
}
