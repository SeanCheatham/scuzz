#include "scuzz_rt.h"
#include "net_transport.h"
#include <stdio.h>

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

int sz_net_host_equal(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *b) {
    unsigned char ca = (unsigned char)*a++;
    unsigned char cb = (unsigned char)*b++;
    if (ca >= 'A' && ca <= 'Z')
      ca = (unsigned char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z')
      cb = (unsigned char)(cb + 32);
    if (ca != cb)
      return 0;
  }
  return *a == 0 && *b == 0;
}

static SzAdt *link_result(int ok, const char *text) {
  SzString *s = sz_string_from_cstr(text);
  SzAdt *r = sz_adt_new(ok, s);
  sz_release(s);
  return r;
}

/* RFC 3986 removes dot segments from the path, not from the query. */
static void link_clean_path(const char *input, char *out) {
  size_t i = 0, n = 0;
  while (input[i]) {
    if (strncmp(input + i, "/./", 3) == 0) i += 2;
    else if (strcmp(input + i, "/.") == 0) { out[n++] = '/'; break; }
    else if (strncmp(input + i, "/../", 4) == 0 || strcmp(input + i, "/..") == 0) {
      i += 3;
      while (n && out[n - 1] != '/') n--;
      if (n) n--;
      if (!input[i]) { out[n++] = '/'; break; }
    } else {
      if (input[i] == '/') out[n++] = input[i++];
      while (input[i] && input[i] != '/') out[n++] = input[i++];
    }
  }
  out[n] = 0;
}

static int link_uri(const char *p) {
  for (; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '%') {
      if (!p[1] || !p[2] || !strchr("0123456789abcdefABCDEF", p[1]) ||
          !strchr("0123456789abcdefABCDEF", p[2])) return 0;
      p += 2;
    } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || strchr("-._~:/?#[]@!$&'()*+,;=", c)))
      return 0;
  }
  return 1;
}

static int link_resolve(const char *base, const char *ref, char *out, size_t cap) {
  char host[256], target_host[256], path[1024], target_path[1024];
  char candidate[2048], clean[1024], query[1024];
  int port, v6, tls, target_port, target_v6, target_tls, n;
  size_t origin, cut;
  const char *colon;
  if (!link_uri(base) || !link_uri(ref) ||
      sz_net_parse_http_url(base, host, sizeof host, path, sizeof path,
                            &port, &v6, &tls) != 1)
    return 0;
  origin = (tls ? 8 : 7) + strcspn(base + (tls ? 8 : 7), "/?#");
  colon = strchr(ref, ':');
  if (colon && (size_t)(colon - ref) < strcspn(ref, "/?#"))
    n = snprintf(candidate, sizeof candidate, "%s", ref);
  else if (strncmp(ref, "//", 2) == 0)
    n = snprintf(candidate, sizeof candidate, "%s:%s", tls ? "https" : "http", ref);
  else if (*ref == '/')
    n = snprintf(candidate, sizeof candidate, "%.*s%s", (int)origin, base, ref);
  else {
    if (*ref == '?' || (*ref && *ref != '#')) {
      path[strcspn(path, "?")] = 0;
      if (*ref != '?') {
        char *slash = strrchr(path, '/');
        if (slash) slash[1] = 0;
      }
    }
    n = snprintf(candidate, sizeof candidate, "%.*s%s%s", (int)origin, base, path, ref);
  }
  if (n < 0 || (size_t)n >= sizeof candidate) return 0;
  for (cut = 0; candidate[cut] && candidate[cut] != ':'; cut++)
    if (candidate[cut] >= 'A' && candidate[cut] <= 'Z') candidate[cut] += 32;
  if (sz_net_parse_http_url(candidate, target_host, sizeof target_host,
                            target_path, sizeof target_path,
                            &target_port, &target_v6, &target_tls) != 1 ||
      port != target_port || tls != target_tls || v6 != target_v6)
    return 0;
  if (v6) {
    struct in6_addr a, b;
    if (inet_pton(AF_INET6, host, &a) != 1 ||
        inet_pton(AF_INET6, target_host, &b) != 1 || memcmp(&a, &b, sizeof a))
      return 0;
  } else if (!sz_net_host_equal(host, target_host)) return 0;
  cut = strcspn(target_path, "?");
  strcpy(query, target_path + cut);
  target_path[cut] = 0;
  link_clean_path(target_path, clean);
  n = snprintf(out, cap, "%.*s%s%s", (int)origin, base, clean, query);
  return n >= 0 && (size_t)n < cap;
}

static int link_token(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || (c && strchr("!#$%&'*+-.^_`|~", c));
}

static void link_spaces(const char **p) {
  while (**p == ' ' || **p == '\t') (*p)++;
}

static int link_next_relation(char *value) {
  char *p = value;
  while (*p) {
    char *start, saved;
    while (*p == ' ') p++;
    start = p;
    while (*p && *p != ' ') p++;
    saved = *p;
    *p = 0;
    if (sz_net_host_equal(start, "next")) return 1;
    *p = saved;
  }
  return 0;
}

/* A next link uses the current origin. Links with an anchor do not apply. */
SzAdt *sz_net_next_link(SzString *base_value, SzString *header_value) {
  const char *base = sz_string_cstr(base_value), *p = sz_string_cstr(header_value);
  char target[2048], result[2048] = "", value[16385], name[128];
  size_t i, len = (size_t)sz_string_len(header_value);
  int found = 0;
  if (sz_net_url_has_bad_bytes(base, (size_t)sz_string_len(base_value)) || len > 16384)
    return link_result(0, "Invalid Link header or base URL");
  if (!link_resolve(base, "", result, sizeof result))
    return link_result(0, "Invalid base URL");
  result[0] = 0;
  for (i = 0; i < len; i++)
    if (((unsigned char)p[i] < 32 && p[i] != '\t') || (unsigned char)p[i] == 127)
      return link_result(0, "Invalid Link header");
  while (*p) {
    int rel_seen = 0, next = 0, anchor = 0;
    link_spaces(&p);
    if (*p == ',') { p++; continue; }
    if (!*p) break;
    if (*p++ != '<') return link_result(0, "Invalid Link header");
    i = 0;
    while (*p && *p != '>') {
      if (i + 1 >= sizeof target || (unsigned char)*p <= 32 || *p == '<')
        return link_result(0, "Invalid Link target");
      target[i++] = *p++;
    }
    target[i] = 0;
    if (*p++ != '>') return link_result(0, "Invalid Link header");
    link_spaces(&p);
    while (*p == ';') {
      p++;
      link_spaces(&p);
      i = 0;
      while (link_token((unsigned char)*p)) {
        if (i + 1 >= sizeof name) return link_result(0, "Invalid Link parameter");
        name[i++] = *p++;
      }
      name[i] = 0;
      if (!i) return link_result(0, "Invalid Link parameter");
      link_spaces(&p);
      i = 0;
      if (*p == '=') {
        p++;
        link_spaces(&p);
        if (*p == '"') {
          p++;
          while (*p && *p != '"') {
            if (*p == '\\') { p++; if (!*p) return link_result(0, "Invalid Link quote"); }
            value[i++] = *p++;
          }
          if (*p++ != '"') return link_result(0, "Invalid Link quote");
        } else {
          while (link_token((unsigned char)*p)) value[i++] = *p++;
          if (!i) return link_result(0, "Invalid Link parameter");
        }
      }
      value[i] = 0;
      if (sz_net_host_equal(name, "anchor")) anchor = 1;
      if (sz_net_host_equal(name, "rel") && !rel_seen) {
        next = link_next_relation(value);
        rel_seen = 1;
      }
      link_spaces(&p);
    }
    if (*p && *p != ',') return link_result(0, "Invalid Link header");
    if (next && !anchor) {
      if (found++) return link_result(0, "Ambiguous next Link");
      if (!link_resolve(base, target, result, sizeof result))
        return link_result(0, "Next Link must use the current origin");
    }
  }
  return link_result(1, result);
}


static int http_tchar(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
         c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' ||
         c == '-' || c == '.' || c == '^' || c == '_' || c == '`' ||
         c == '|' || c == '~';
}

int sz_net_http_name_equal(const char *a, const char *b) {
  size_t n;
  if (!a || !b)
    return 0;
  n = strlen(a);
  return strlen(b) == n && sz_net_host_equal(a, b);
}

int sz_net_http_header_skip(const char *name) {
  return sz_net_http_name_equal(name, "Content-Length") ||
         sz_net_http_name_equal(name, "Connection") ||
         sz_net_http_name_equal(name, "Transfer-Encoding");
}

int sz_net_http_header_valid(const char *name, const char *val) {
  size_t i;
  if (!name || !name[0] || !val)
    return 0;
  for (i = 0; name[i]; i++) {
    if (!http_tchar((unsigned char)name[i]))
      return 0;
  }
  for (i = 0; val[i]; i++) {
    unsigned char c = (unsigned char)val[i];
    if (c == 0 || c == '\r' || c == '\n')
      return 0;
  }
  return 1;
}


const char *sz_net_http_op(const char *method) {
  if (method && strcmp(method, "POST") == 0)
    return "Net.httpPost";
  if (method && strcmp(method, "PUT") == 0)
    return "Net.httpPut";
  if (method && strcmp(method, "PATCH") == 0)
    return "Net.httpPatch";
  if (method && strcmp(method, "DELETE") == 0)
    return "Net.httpDelete";
  if (method && strcmp(method, "HEAD") == 0)
    return "Net.httpHead";
  return "Net.httpGet";
}
