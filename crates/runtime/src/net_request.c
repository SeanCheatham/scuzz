#include "scuzz_rt.h"
#include "rt_util.h"
#include "net_transport.h"

static int request_headers_valid(SzMap *headers) {
  SzList *rows = sz_map_to_list(headers);
  SzList *it;
  SzMap *seen = NULL;
  size_t total = 0;
  int valid = 1;
  for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = (SzPair *)sz_list_head(it);
    SzString *name = kv ? (SzString *)kv->left : NULL;
    SzString *value = kv ? (SzString *)kv->right : NULL;
    const char *n = name ? sz_string_cstr(name) : "";
    const char *v = value ? sz_string_cstr(value) : "";
    SzString *lower;
    SzMap *next;
    if (!name || !value || strlen(n) != (size_t)sz_string_len(name) ||
        strlen(v) != (size_t)sz_string_len(value) || !sz_net_http_header_valid(n, v) ||
        sz_net_http_header_skip(n) || sz_net_http_name_equal(n, "Host")) {
      valid = 0;
      break;
    }
    for (size_t j = 0; v[j]; j++) {
      unsigned char c = (unsigned char)v[j];
      if ((c < 32 && c != '\t') || c == 127) valid = 0;
    }
    if (!valid) break;
    total += strlen(n) + strlen(v) + 4;
    lower = sz_string_to_lower(name);
    if (total > 16384 || sz_map_contains(seen, lower)) {
      sz_release(lower);
      valid = 0;
      break;
    }
    next = sz_map_set(seen, lower, value, 1);
    sz_release(seen);
    sz_release(lower);
    seen = next;
  }
  sz_release(seen);
  sz_release(rows);
  return valid;
}

static void *http_dispatch(void *env) {
  (void)env;
  return (void *)(intptr_t)(sz_testrt_net_is_fake() ? 1 : 0);
}

static SzIo *http_after_dispatch(void *value, void *env) {
  SzPair *pack = (SzPair *)env;
  SzString *url = pack ? (SzString *)pack->left : NULL;
  SzPair *inner = pack ? (SzPair *)pack->right : NULL;
  SzString *ms = inner ? (SzString *)inner->left : NULL;
  SzPair *payload = inner ? (SzPair *)inner->right : NULL;
  SzMap *headers = payload ? (SzMap *)payload->left : NULL;
  SzString *body = payload ? (SzString *)payload->right : NULL;
  const char *method = ms ? sz_string_cstr(ms) : "GET";
  char host[256], path[1024];
  int port, v6, tls;
  if (!url || sz_net_url_has_bad_bytes(sz_string_cstr(url), (size_t)sz_string_len(url)) ||
      !sz_net_parse_http_url(sz_string_cstr(url), host, sizeof host, path, sizeof path,
                             &port, &v6, &tls))
    return fail_drop(sz_error_new(6, "Net: invalid URL"));
  if (!request_headers_valid(headers))
    return fail_drop(sz_error_new(6, "Net: invalid request headers"));
  if ((intptr_t)value)
    return sz_testrt_net_http_req(method, url, headers, body);
  sz_timeline_log_cstr(sz_net_http_op(method), sz_string_cstr(url));
  return sz_net_live_http_req(method, url, headers, body);
}

static SzIo *sz_net_http_req(const char *method, SzString *url, SzMap *headers, SzString *body) {
  SzString *ms;
  SzPair *inner;
  SzPair *pack;
  SzIo *io;
  if (!url)
    sz_panic("sz_net_http_req(null)");
  ms = sz_string_from_cstr(method ? method : "GET");
  SzPair *payload = sz_pair_new(headers, body);
  inner = sz_pair_new(ms, payload);
  sz_release(payload);
  pack = sz_pair_new(url, inner);
  sz_release(ms);
  sz_release(inner);
  io = fm_drop(sz_io_delay(http_dispatch, pack), http_after_dispatch, pack);
  sz_release(pack);
  return io;
}

SzIo *sz_net_http_get(SzString *url, SzMap *headers) { return sz_net_http_req("GET", url, headers, NULL); }

SzIo *sz_net_http_post(SzString *url, SzMap *headers, SzString *body) {
  if (!url || !body)
    sz_panic("sz_net_http_post(null)");
  return sz_net_http_req("POST", url, headers, body);
}

SzIo *sz_net_http_put(SzString *url, SzMap *headers, SzString *body) {
  if (!url || !body)
    sz_panic("sz_net_http_put(null)");
  return sz_net_http_req("PUT", url, headers, body);
}

SzIo *sz_net_http_patch(SzString *url, SzMap *headers, SzString *body) {
  if (!url || !body)
    sz_panic("sz_net_http_patch(null)");
  return sz_net_http_req("PATCH", url, headers, body);
}

SzIo *sz_net_http_delete(SzString *url, SzMap *headers) {
  return sz_net_http_req("DELETE", url, headers, NULL);
}

SzIo *sz_net_http_head(SzString *url, SzMap *headers) { return sz_net_http_req("HEAD", url, headers, NULL); }
