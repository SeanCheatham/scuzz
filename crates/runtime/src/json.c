#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Blessed Json parse/stringify. Pure, like Str.concat. ADT tags match the
 * injected Json enum: Null | Bool | Int | Float | Str | Arr | Obj. */

#define JSON_NULL 0
#define JSON_BOOL 1
#define JSON_INT 2
#define JSON_FLOAT 3
#define JSON_STR 4
#define JSON_ARR 5
#define JSON_OBJ 6
#define JSON_DEPTH 256

typedef struct {
  const char *s;
  size_t n;
  size_t i;
  int depth;
} Jp;

typedef struct {
  char *p;
  size_t n;
  size_t cap;
} Jb;

static void *box_f64(double x) {
  int64_t bits = 0;
  memcpy(&bits, &x, sizeof bits);
  return sz_box_i64(bits);
}

static double unbox_f64(const void *p) {
  int64_t bits = sz_unbox_i64(p);
  double x = 0.0;
  memcpy(&x, &bits, sizeof x);
  return x;
}

static SzAdt *json_mk(int32_t tag, void *payload) {
  SzAdt *a = sz_adt_new(tag, payload);
  sz_release(payload);
  return a;
}

static SzAdt *json_null(void) { return sz_adt_new(JSON_NULL, NULL); }

static SzAdt *json_bool(int v) { return json_mk(JSON_BOOL, sz_box_i64(v ? 1 : 0)); }

static SzAdt *json_int(int64_t n) { return json_mk(JSON_INT, sz_box_i64(n)); }

static SzAdt *json_float(double x) { return json_mk(JSON_FLOAT, box_f64(x)); }

static SzAdt *json_str(SzString *s) { return json_mk(JSON_STR, s); }

static void jp_fail(Jp *p, const char *msg) {
  (void)p;
  sz_panic(msg);
}

static int jp_at(const Jp *p) {
  return p->i < p->n ? (unsigned char)p->s[p->i] : 0;
}

static void jp_skip(Jp *p) {
  while (p->i < p->n) {
    char c = p->s[p->i];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      break;
    p->i++;
  }
}

static void jp_eat(Jp *p, char c) {
  jp_skip(p);
  if (jp_at(p) != (unsigned char)c)
    jp_fail(p, "Json.parse: unexpected token");
  p->i++;
}

static int jp_starts(const Jp *p, const char *lit) {
  size_t n = strlen(lit);
  return p->i + n <= p->n && memcmp(p->s + p->i, lit, n) == 0;
}

static SzAdt *jp_value(Jp *p);

static int hex_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static SzString *jp_string(Jp *p) {
  char *buf;
  size_t cap = 16;
  size_t n = 0;
  jp_skip(p);
  if (jp_at(p) != '"')
    jp_fail(p, "Json.parse: expected string");
  p->i++;
  buf = (char *)sz_alloc(cap);
  while (p->i < p->n) {
    unsigned char c = (unsigned char)p->s[p->i++];
    if (c == '"') {
      SzString *s;
      buf[n] = '\0';
      s = sz_string_from_bytes(buf, n);
      sz_free(buf);
      return s;
    }
    if (c == '\\') {
      if (p->i >= p->n)
        jp_fail(p, "Json.parse: bad string escape");
      c = (unsigned char)p->s[p->i++];
      switch (c) {
      case '"':
      case '\\':
      case '/':
        break;
      case 'b':
        c = '\b';
        break;
      case 'f':
        c = '\f';
        break;
      case 'n':
        c = '\n';
        break;
      case 'r':
        c = '\r';
        break;
      case 't':
        c = '\t';
        break;
      case 'u': {
        int i;
        unsigned cp = 0;
        for (i = 0; i < 4; i++) {
          int h;
          if (p->i >= p->n)
            jp_fail(p, "Json.parse: bad unicode escape");
          h = hex_val(p->s[p->i++]);
          if (h < 0)
            jp_fail(p, "Json.parse: bad unicode escape");
          cp = (cp << 4) | (unsigned)h;
        }
        if (cp < 0x80) {
          c = (unsigned char)cp;
        } else if (cp < 0x800) {
          if (n + 2 >= cap) {
            size_t nc = cap * 2;
            char *nb = (char *)sz_alloc(nc);
            memcpy(nb, buf, n);
            sz_free(buf);
            buf = nb;
            cap = nc;
          }
          buf[n++] = (char)(0xC0 | (cp >> 6));
          buf[n++] = (char)(0x80 | (cp & 0x3F));
          continue;
        } else {
          if (n + 3 >= cap) {
            size_t nc = cap * 2;
            char *nb = (char *)sz_alloc(nc);
            memcpy(nb, buf, n);
            sz_free(buf);
            buf = nb;
            cap = nc;
          }
          buf[n++] = (char)(0xE0 | (cp >> 12));
          buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          buf[n++] = (char)(0x80 | (cp & 0x3F));
          continue;
        }
        break;
      }
      default:
        jp_fail(p, "Json.parse: bad string escape");
      }
    } else if (c < 0x20) {
      jp_fail(p, "Json.parse: control in string");
    }
    if (n + 1 >= cap) {
      size_t nc = cap * 2;
      char *nb = (char *)sz_alloc(nc);
      memcpy(nb, buf, n);
      sz_free(buf);
      buf = nb;
      cap = nc;
    }
    buf[n++] = (char)c;
  }
  jp_fail(p, "Json.parse: unterminated string");
  return NULL;
}

static SzAdt *jp_number(Jp *p) {
  size_t start;
  int is_float = 0;
  char tmp[128];
  size_t len;
  jp_skip(p);
  start = p->i;
  if (jp_at(p) == '-')
    p->i++;
  if (jp_at(p) < '0' || jp_at(p) > '9')
    jp_fail(p, "Json.parse: bad number");
  if (jp_at(p) == '0') {
    p->i++;
  } else {
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  if (jp_at(p) == '.') {
    is_float = 1;
    p->i++;
    if (jp_at(p) < '0' || jp_at(p) > '9')
      jp_fail(p, "Json.parse: bad number");
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  if (jp_at(p) == 'e' || jp_at(p) == 'E') {
    is_float = 1;
    p->i++;
    if (jp_at(p) == '+' || jp_at(p) == '-')
      p->i++;
    if (jp_at(p) < '0' || jp_at(p) > '9')
      jp_fail(p, "Json.parse: bad number");
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  len = p->i - start;
  if (len >= sizeof tmp)
    jp_fail(p, "Json.parse: number too long");
  memcpy(tmp, p->s + start, len);
  tmp[len] = '\0';
  if (!is_float) {
    char *end = NULL;
    long long v = strtoll(tmp, &end, 10);
    if (end && *end == '\0' && v >= INT64_MIN && v <= INT64_MAX)
      return json_int((int64_t)v);
  }
  {
    char *end = NULL;
    double x = strtod(tmp, &end);
    if (!end || *end != '\0')
      jp_fail(p, "Json.parse: bad number");
    return json_float(x);
  }
}

static SzAdt *jp_array(Jp *p) {
  SzList *acc;
  SzAdt *out;
  jp_eat(p, '[');
  jp_skip(p);
  acc = sz_list_nil();
  if (jp_at(p) != ']') {
    for (;;) {
      SzAdt *v = jp_value(p);
      SzList *old = acc;
      acc = sz_list_cons(v, old);
      sz_release(v);
      sz_release(old);
      jp_skip(p);
      if (jp_at(p) == ',') {
        p->i++;
        continue;
      }
      break;
    }
  }
  jp_eat(p, ']');
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    out = json_mk(JSON_ARR, rev);
  }
  return out;
}

static SzAdt *jp_object(Jp *p) {
  SzList *acc;
  SzAdt *out;
  jp_eat(p, '{');
  jp_skip(p);
  acc = sz_list_nil();
  if (jp_at(p) != '}') {
    for (;;) {
      SzString *k = jp_string(p);
      SzAdt *v;
      SzPair *ent;
      SzList *old;
      jp_eat(p, ':');
      v = jp_value(p);
      ent = sz_pair_new(k, v);
      sz_release(k);
      sz_release(v);
      old = acc;
      acc = sz_list_cons(ent, old);
      sz_release(ent);
      sz_release(old);
      jp_skip(p);
      if (jp_at(p) == ',') {
        p->i++;
        continue;
      }
      break;
    }
  }
  jp_eat(p, '}');
  {
    SzList *rev = sz_list_reverse(acc);
    sz_release(acc);
    out = json_mk(JSON_OBJ, rev);
  }
  return out;
}

static SzAdt *jp_value(Jp *p) {
  jp_skip(p);
  if (p->depth > JSON_DEPTH)
    jp_fail(p, "Json.parse: too deep");
  p->depth++;
  if (jp_at(p) == '{') {
    SzAdt *v = jp_object(p);
    p->depth--;
    return v;
  }
  if (jp_at(p) == '[') {
    SzAdt *v = jp_array(p);
    p->depth--;
    return v;
  }
  if (jp_at(p) == '"') {
    SzString *s = jp_string(p);
    p->depth--;
    return json_str(s);
  }
  if (jp_at(p) == '-' || (jp_at(p) >= '0' && jp_at(p) <= '9')) {
    SzAdt *v = jp_number(p);
    p->depth--;
    return v;
  }
  if (jp_starts(p, "true")) {
    p->i += 4;
    p->depth--;
    return json_bool(1);
  }
  if (jp_starts(p, "false")) {
    p->i += 5;
    p->depth--;
    return json_bool(0);
  }
  if (jp_starts(p, "null")) {
    p->i += 4;
    p->depth--;
    return json_null();
  }
  jp_fail(p, "Json.parse: unexpected token");
  return NULL;
}

SzAdt *sz_json_parse(SzString *s) {
  Jp p;
  SzAdt *v;
  if (!s)
    sz_panic("Json.parse(null)");
  p.s = sz_string_cstr(s);
  p.n = sz_string_len(s);
  p.i = 0;
  p.depth = 0;
  v = jp_value(&p);
  jp_skip(&p);
  if (p.i != p.n) {
    sz_release(v);
    sz_panic("Json.parse: trailing junk");
  }
  return v;
}

static void jb_put(Jb *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 64;
    char *np;
    while (nc < b->n + n + 1)
      nc *= 2;
    np = (char *)sz_alloc(nc);
    if (b->p && b->n)
      memcpy(np, b->p, b->n);
    sz_free(b->p);
    b->p = np;
    b->cap = nc;
  }
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = '\0';
}

static void jb_puts(Jb *b, const char *s) { jb_put(b, s, strlen(s)); }

static void jb_putc(Jb *b, char c) { jb_put(b, &c, 1); }

static void jb_str(Jb *b, const SzString *s) {
  const char *p = s ? sz_string_cstr(s) : "";
  size_t n = s ? sz_string_len(s) : 0;
  size_t i;
  jb_putc(b, '"');
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)p[i];
    switch (c) {
    case '"':
      jb_puts(b, "\\\"");
      break;
    case '\\':
      jb_puts(b, "\\\\");
      break;
    case '\n':
      jb_puts(b, "\\n");
      break;
    case '\r':
      jb_puts(b, "\\r");
      break;
    case '\t':
      jb_puts(b, "\\t");
      break;
    default:
      if (c < 0x20) {
        char u[7];
        snprintf(u, sizeof u, "\\u%04x", c);
        jb_puts(b, u);
      } else {
        jb_putc(b, (char)c);
      }
      break;
    }
  }
  jb_putc(b, '"');
}

static void jb_value(Jb *b, const SzAdt *j);

static void jb_array(Jb *b, SzList *xs) {
  int first = 1;
  jb_putc(b, '[');
  while (xs && !sz_list_is_empty(xs)) {
    if (!first)
      jb_putc(b, ',');
    first = 0;
    jb_value(b, (SzAdt *)sz_list_head(xs));
    xs = sz_list_tail(xs);
  }
  jb_putc(b, ']');
}

static void jb_object(Jb *b, SzList *xs) {
  int first = 1;
  jb_putc(b, '{');
  while (xs && !sz_list_is_empty(xs)) {
    SzPair *ent = (SzPair *)sz_list_head(xs);
    if (!first)
      jb_putc(b, ',');
    first = 0;
    jb_str(b, (SzString *)sz_pair_left(ent));
    jb_putc(b, ':');
    jb_value(b, (SzAdt *)sz_pair_right(ent));
    xs = sz_list_tail(xs);
  }
  jb_putc(b, '}');
}

static void jb_value(Jb *b, const SzAdt *j) {
  int32_t tag = sz_adt_tag(j);
  void *pay = sz_adt_payload(j);
  switch (tag) {
  case JSON_NULL:
    jb_puts(b, "null");
    break;
  case JSON_BOOL:
    jb_puts(b, sz_unbox_i64(pay) ? "true" : "false");
    break;
  case JSON_INT: {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%lld", (long long)sz_unbox_i64(pay));
    jb_puts(b, tmp);
    break;
  }
  case JSON_FLOAT: {
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%.17g", unbox_f64(pay));
    if (!strchr(tmp, '.') && !strchr(tmp, 'e') && !strchr(tmp, 'E'))
      strcat(tmp, ".0");
    jb_puts(b, tmp);
    break;
  }
  case JSON_STR:
    jb_str(b, (SzString *)pay);
    break;
  case JSON_ARR:
    jb_array(b, (SzList *)pay);
    break;
  case JSON_OBJ:
    jb_object(b, (SzList *)pay);
    break;
  default:
    sz_panic("Json.stringify: bad tag");
  }
}

SzString *sz_json_stringify(SzAdt *j) {
  Jb b;
  SzString *s;
  if (!j)
    sz_panic("Json.stringify(null)");
  memset(&b, 0, sizeof b);
  jb_value(&b, j);
  s = sz_string_from_bytes(b.p ? b.p : "", b.n);
  sz_free(b.p);
  return s;
}
