/* Browser Net. HTTP GET uses fetch on this thread. Other Net calls fail. */
#include "scuzz_rt.h"

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#define WEB_BODY_MAX (1024 * 1024)
#define WEB_HDR_MAX 16384

int sz_net_host_equal(const char *a, const char *b);

static SzIo *no_net(void) { return sz_io_fail_cstr("Net: not available on web"); }

static SzIo *fm_drop(SzIo *inner, SzCont cont, void *env) {
  SzIo *io = sz_io_flatmap(inner, cont, env);
  sz_release(inner);
  return io;
}

static int web_live_replay(void) {
  const char *live = getenv("SCUZZ_NET_LIVE");
  const char *tr = getenv("SCUZZ_TESTRT");
  return live && live[0] == '1' && tr && tr[0] == '1' && !sz_testrt_net_is_fake();
}

static int web_loopback(const char *host) {
  return sz_net_host_equal(host, "127.0.0.1") || sz_net_host_equal(host, "::1") ||
         sz_net_host_equal(host, "localhost");
}

/* status_out receives the HTTP status. A negative return is an error. */
EM_ASYNC_JS(int, web_http_get, (const char *url, const char *req_headers, char *body, int body_cap, char *hdr, int hdr_cap, int *status_out), {
  const target = UTF8ToString(Number(url));
  const raw = UTF8ToString(Number(req_headers));
  const headers = new Headers();
  if (raw) {
    for (const line of raw.split('\n')) {
      const cut = line.indexOf(':');
      if (cut <= 0) continue;
      headers.append(line.slice(0, cut), line.slice(cut + 1));
    }
  }
  let response;
  try {
    response = await fetch(target, {method: 'GET', headers, redirect: 'error', cache: 'no-store'});
  } catch (error) {
    return -1;
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (bytes.length > body_cap) return -2;
  if (bytes.length) HEAPU8.set(bytes, Number(body));
  const lines = [];
  response.headers.forEach((value, name) => lines.push(name + ':' + value));
  const encoded = new TextEncoder().encode(lines.join('\n'));
  if (encoded.length + 1 > hdr_cap) return -3;
  if (encoded.length) HEAPU8.set(encoded, Number(hdr));
  HEAPU8[Number(hdr) + encoded.length] = 0;
  const status = response.status >>> 0;
  const slot = Number(status_out);
  HEAPU8[slot] = status & 255;
  HEAPU8[slot + 1] = (status >>> 8) & 255;
  HEAPU8[slot + 2] = (status >>> 16) & 255;
  HEAPU8[slot + 3] = (status >>> 24) & 255;
  return bytes.length;
});

typedef struct {
  SzString *url;
  SzMap *headers;
} WebReq;

static SzEither *web_err(const char *msg) {
  SzError *err = sz_error_new(6, msg);
  SzEither *out = sz_either_left(err);
  sz_release(err);
  return out;
}

static char *request_headers(SzMap *headers) {
  SzList *rows = sz_map_to_list(headers);
  size_t n = 1;
  char *buf;
  char *p;
  SzList *it;
  for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = sz_list_head(it);
    n += strlen(sz_string_cstr(kv->left)) + strlen(sz_string_cstr(kv->right)) + 2;
  }
  buf = sz_alloc(n);
  p = buf;
  for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = sz_list_head(it);
    const char *name = sz_string_cstr(kv->left);
    const char *value = sz_string_cstr(kv->right);
    size_t ln = strlen(name);
    size_t lv = strlen(value);
    memcpy(p, name, ln);
    p += ln;
    *p++ = ':';
    memcpy(p, value, lv);
    p += lv;
    *p++ = '\n';
  }
  *p = '\0';
  sz_release(rows);
  return buf;
}

static SzMap *headers_from_blob(const char *blob) {
  SzMap *map = NULL;
  const char *p = blob ? blob : "";
  while (*p) {
    const char *nl = strchr(p, '\n');
    size_t line = nl ? (size_t)(nl - p) : strlen(p);
    const char *colon = memchr(p, ':', line);
    if (colon && colon > p) {
      SzString *name = sz_string_from_bytes(p, (size_t)(colon - p));
      SzString *value = sz_string_from_bytes(colon + 1, line - (size_t)(colon - p) - 1);
      SzMap *next = sz_map_set(map, name, value, 1);
      sz_release(map);
      sz_release(name);
      sz_release(value);
      map = next;
    }
    p += line;
    if (*p == '\n')
      p++;
  }
  return map;
}

static void *web_get_thunk(void *env) {
  WebReq *req = env;
  char host[256];
  char path[1024];
  char *blob;
  char *body;
  char *hdr;
  int port = 0;
  int v6 = 0;
  int tls = 0;
  int status = 0;
  int n;
  const char *url = req && req->url ? sz_string_cstr(req->url) : "";
  if (sz_net_parse_http_url(url, host, sizeof host, path, sizeof path, &port, &v6, &tls) != 1)
    return web_err("Net.httpGet: invalid URL");
  if (web_live_replay() && !web_loopback(host))
    return web_err("Net.httpGet: live replay rejects non-loopback");
  blob = request_headers(req->headers);
  body = sz_alloc(WEB_BODY_MAX);
  hdr = sz_alloc(WEB_HDR_MAX);
  hdr[0] = '\0';
  n = web_http_get(url, blob, body, WEB_BODY_MAX, hdr, WEB_HDR_MAX, &status);
  sz_free(blob);
  if (n < 0) {
    sz_free(body);
    sz_free(hdr);
    if (n == -2)
      return web_err("Net.httpGet: response too large");
    if (n == -3)
      return web_err("Net.httpGet: response headers too large");
    return web_err("Net.httpGet: request failed");
  }
  {
    SzString *text = sz_string_from_bytes(body, (size_t)n);
    SzMap *headers = headers_from_blob(hdr);
    void *resp = sz_net_http_resp((int64_t)status, headers, text);
    SzEither *out = sz_either_right(resp);
    sz_release(text);
    sz_release(headers);
    sz_release(resp);
    sz_free(body);
    sz_free(hdr);
    return out;
  }
}

static SzIo *web_after(void *value, void *env) {
  SzEither *either = value;
  SzIo *io;
  (void)env;
  if (!either || !either->is_right) {
    SzError *err = either ? either->as.left : NULL;
    if (err)
      sz_retain(err);
    else
      err = sz_error_new(6, "Net.httpGet: request failed");
    sz_release(either);
    io = sz_io_fail(err);
    sz_release(err);
    return io;
  }
  sz_retain(either->as.right);
  io = sz_io_pure(either->as.right);
  sz_release(either->as.right);
  sz_release(either);
  return io;
}

static void *web_cleanup(void *env) {
  WebReq *req = env;
  if (!req)
    return NULL;
  sz_release(req->url);
  req->url = NULL;
  sz_release(req->headers);
  req->headers = NULL;
  return NULL;
}

SzIo *sz_net_live_http_req(const char *method, SzString *url, SzMap *headers, SzString *body) {
  WebReq *req;
  SzIo *io;
  SzIo *fin;
  SzIo *ens;
  (void)body;
  if (!method || strcmp(method, "GET") != 0)
    return no_net();
  if (!url)
    sz_panic("sz_net_live_http_req(null)");
  req = sz_rc_alloc(sizeof *req, SZ_RC_BOX);
  memset(req, 0, sizeof *req);
  sz_retain(url);
  req->url = url;
  if (headers)
    sz_retain(headers);
  req->headers = headers;
  io = fm_drop(sz_io_delay(web_get_thunk, req), web_after, NULL);
  fin = sz_io_delay(web_cleanup, req);
  ens = sz_io_ensure(io, fin);
  sz_release(io);
  sz_release(fin);
  sz_release(req);
  return ens;
}

SzIo *sz_net_live_http_get_to_file(SzString *url, SzMap *headers, SzString *path) {
  (void)url;
  (void)headers;
  (void)path;
  return no_net();
}

SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return no_net();
}
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return no_net();
}
SzIo *sz_net_serve_once_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return no_net();
}
SzIo *sz_net_serve_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return no_net();
}
SzIo *sz_net_serve_once_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return no_net();
}
SzIo *sz_net_serve_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return no_net();
}
