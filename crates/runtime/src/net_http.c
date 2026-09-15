#include "scuzz_rt.h"

#include <arpa/inet.h>
#include <string.h>

/* Shared HTTP values and parsing. These functions do not open sockets. */

void *sz_net_http_resp(int64_t status, SzMap *headers, SzString *body) {
  void *st;
  SzString *b = body;
  SzPair *inner;
  SzPair *outer;
  int drop_b = 0;
  if (!b) {
    b = sz_string_from_cstr("");
    drop_b = 1;
  }
  st = sz_box_i64(status);
  inner = sz_pair_new(headers, b);
  outer = sz_pair_new(st, inner);
  sz_release(st);
  sz_release(inner);
  if (drop_b)
    sz_release(b);
  return outer;
}

int sz_net_url_has_bad_bytes(const char *s, size_t n) {
  size_t i;
  if (!s)
    return 1;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c <= 32 || c == 127)
      return 1;
  }
  return 0;
}

static int parse_port_digits(const char *s, const char **end, int *port) {
  unsigned long v = 0;
  int digits = 0;
  if (!s || s[0] < '0' || s[0] > '9')
    return 0;
  while (s[0] >= '0' && s[0] <= '9') {
    v = v * 10UL + (unsigned long)(s[0] - '0');
    s++;
    digits++;
    if (digits > 5 || v > 65535UL)
      return 0;
  }
  if (v < 1UL)
    return 0;
  *port = (int)v;
  if (end)
    *end = s;
  return 1;
}

int sz_net_parse_http_url(const char *url, char *host, size_t host_sz,
                          char *path, size_t path_sz, int *port,
                          int *is_v6, int *tls) {
  const char *p, *end, *rest;
  size_t hlen, plen, prefix;
  if (!url || !host || !path || !port || !is_v6 || !tls || path_sz < 2)
    return 0;
  if (sz_net_url_has_bad_bytes(url, strlen(url)))
    return 0;
  *tls = 0;
  *is_v6 = 0;
  *port = 80;
  if (strncmp(url, "https://", 8) == 0) {
    *tls = 1;
    *port = 443;
    p = url + 8;
  } else if (strncmp(url, "http://", 7) == 0) {
    p = url + 7;
  } else
    return 0;
  end = p + strcspn(p, "/?#");
  if (*p == '[') {
    const char *rb = memchr(p, ']', (size_t)(end - p));
    struct in6_addr address;
    if (!rb)
      return 0;
    hlen = (size_t)(rb - p - 1);
    if (!hlen || hlen >= host_sz)
      return 0;
    memcpy(host, p + 1, hlen);
    host[hlen] = 0;
    if (inet_pton(AF_INET6, host, &address) != 1)
      return 0;
    *is_v6 = 1;
    p = rb + 1;
  } else {
    const char *colon = memchr(p, ':', (size_t)(end - p));
    const char *host_end = colon ? colon : end;
    hlen = (size_t)(host_end - p);
    if (!hlen || hlen >= host_sz)
      return 0;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (strpbrk(host, "@[]"))
      return 0;
    p = host_end;
  }
  if (p != end) {
    if (*p != ':')
      return 0;
    if (!parse_port_digits(p + 1, &rest, port) || rest != end)
      return -1;
  }
  /* The request target contains the path and query. It excludes the fragment. */
  plen = strcspn(end, "#");
  prefix = *end != '/';
  if (plen + prefix >= path_sz)
    return 0;
  if (prefix)
    path[0] = '/';
  memcpy(path + prefix, end, plen);
  path[plen + prefix] = 0;
  return 1;
}

/* Request names use lowercase. Values have no outer space or tab. */
SzMap *sz_net_request_headers(SzMap *headers) {
  SzList *rows = sz_map_to_list(headers);
  SzList *it;
  SzMap *out = NULL;
  for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = (SzPair *)sz_list_head(it);
    SzString *name = sz_string_to_lower((SzString *)kv->left);
    const char *v = sz_string_cstr((SzString *)kv->right);
    size_t n = (size_t)sz_string_len((SzString *)kv->right);
    SzString *value;
    SzMap *next;
    while (n && (*v == ' ' || *v == '\t')) { v++; n--; }
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
    value = sz_string_from_bytes(v, n);
    next = sz_map_set(out, name, value, 1);
    sz_release(out);
    sz_release(name);
    sz_release(value);
    out = next;
  }
  sz_release(rows);
  return out;
}
