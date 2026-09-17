#include "scuzz_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

static int failures;
static void check(int ok, const char *name) {
  if (!ok) { fprintf(stderr, "Apple Net proof fails: %s\n", name); failures++; }
}
static SzMap *header(SzMap *old, const char *name, const char *value) {
  SzString *k = sz_string_from_cstr(name), *v = sz_string_from_cstr(value);
  SzMap *next = sz_map_set(old, k, v, 1);
  sz_release(k); sz_release(v); sz_release(old);
  return next;
}
static SzIo *request(const char *base, const char *path, const char *method, SzMap *headers) {
  char text[2048]; snprintf(text, sizeof text, "%s%s", base, path);
  SzString *url = sz_string_from_cstr(text), *body = sz_string_from_cstr("body-\xc3\xa9");
  SzIo *io = !strcmp(method, "POST") ? sz_net_http_post(url, headers, body) :
             !strcmp(method, "PUT") ? sz_net_http_put(url, headers, body) :
             !strcmp(method, "PATCH") ? sz_net_http_patch(url, headers, body) :
             !strcmp(method, "DELETE") ? sz_net_http_delete(url, headers) :
             !strcmp(method, "HEAD") ? sz_net_http_head(url, headers) : sz_net_http_get(url, headers);
  sz_release(url); sz_release(body); return io;
}
static SzIoResult run(SzIo *io) {
  return sz_io_unsafe_run(io);
}
static void drop(SzIoResult result) { sz_release(result.value); sz_release(result.error); }
static int status(SzIoResult result, int expected) {
  return result.ok && sz_unbox_i64(((SzPair *)result.value)->left) == expected;
}
static void check_http(SzIoResult result, int expected, const char *name) {
  if (status(result, expected)) return;
  check(0, name);
  if (!result.ok && result.error && result.error->message)
    fprintf(stderr, "%s\n", sz_string_cstr(result.error->message));
}
static const char *body(SzIoResult result) {
  return sz_string_cstr(((SzPair *)((SzPair *)result.value)->right)->right);
}
static int descriptors(void) {
  int count = 0;
  for (int fd = 0; fd < 1024; fd++) if (fcntl(fd, F_GETFD) >= 0) count++;
  return count;
}
int scuzz_net_apple_proof(void) {
  const char *base = getenv("SCUZZ_NET_BASE"), *tls = getenv("SCUZZ_NET_TLS");
  if (!base || !tls) return 1;
  const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
  SzMap *headers = header(NULL, "X-Proof", "custom");
  for (size_t i = 0; i < sizeof methods / sizeof *methods; i++) {
    SzIoResult r = run(request(base, "/echo", methods[i], headers));
    check_http(r, 200, methods[i]);
    if (r.ok) {
      check(!strcmp(body(r), !strcmp(methods[i], "HEAD") ? "" : methods[i]), "method response");
      SzString *key = sz_string_from_cstr("x-method");
      SzString *value = sz_map_get_or(((SzPair *)((SzPair *)r.value)->right)->left, key, NULL);
      check(value && !strcmp(sz_string_cstr(value), methods[i]), "lowercase response header");
      sz_release(key);
    }
    drop(r);
  }
  SzIoResult r = run(request(base, "/redirect", "GET", NULL));
  check(status(r, 302), "redirect stays visible"); drop(r);
  r = run(request(base, "/status", "GET", NULL)); check(status(r, 503), "HTTP error is a response"); drop(r);
  const char *invalid[] = {"/large", "/stream-large", "/headers-large"};
  for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
    r = run(request(base, invalid[i], "GET", NULL)); check(!r.ok, invalid[i]); drop(r);
  }
  SzMap *bad = header(NULL, "Host", "wrong");
  r = run(request(base, "/forbidden", "GET", bad)); check(!r.ok, "reject reserved header"); drop(r); sz_release(bad);
  bad = header(header(NULL, "X-Proof", "one"), "x-proof", "two");
  r = run(request(base, "/forbidden", "GET", bad)); check(!r.ok, "reject case duplicate header"); drop(r); sz_release(bad);
  r = run(request("http://bad host", "/", "GET", NULL)); check(!r.ok, "reject invalid URL"); drop(r);
  SzIo *reused = request(base, "/echo", "GET", headers);
  for (int i = 0; i < 3; i++) { sz_retain(reused); r = sz_io_unsafe_run(reused); check(status(r, 200), "reuse request IO"); drop(r); }
  sz_release(reused); sz_release(headers);
  int before = descriptors();
  for (int i = 0; i < 16; i++) {
    SzIo *inner = request(base, "/slow", "GET", NULL), *bounded = sz_io_timeout(30, inner);
    sz_release(inner); r = run(bounded); check(!r.ok, "request cancellation"); drop(r);
  }
  r = run(sz_io_sleep_ms(100)); drop(r);
  check(descriptors() <= before + 4, "cancellation closes request descriptors");
  r = run(request(tls, "/echo", "GET", NULL));
  int trusted = getenv("SCUZZ_NET_TRUSTED") != NULL;
  if (trusted) check_http(r, 200, "platform certificate trust on loopback");
  else check(!r.ok, "platform certificate trust on loopback");
  drop(r);
  if (!trusted) {
    r = run(request("https://example.com", "/", "GET", NULL));
    check(status(r, 200), "platform public certificate trust without OpenSSL paths");
    if (!r.ok) fprintf(stderr, "%s\n", sz_string_cstr(r.error->message));
    drop(r);
  }
  if (!failures) { puts("Apple Net proof ok"); fflush(stdout); }
  return failures ? 1 : 0;
}
#ifndef SCUZZ_NET_IOS
int main(void) { return scuzz_net_apple_proof(); }
#endif
