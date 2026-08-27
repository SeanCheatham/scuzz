#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#include "scuzz_rt.h"

#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif

/* Blessed Json parse/stringify. Pure. ADT tags match the injected Json enum:
 * Null | Bool | Int | Float | Str | Arr | Obj. Both calls return Result
 * (Err = 0, Ok = 1), same as IO.attempt. */

#define JSON_NULL 0
#define JSON_BOOL 1
#define JSON_INT 2
#define JSON_FLOAT 3
#define JSON_STR 4
#define JSON_ARR 5
#define JSON_OBJ 6
#define JSON_DEPTH 256
#define RESULT_ERR 0
#define RESULT_OK 1

typedef struct {
  const char *s;
  size_t n;
  size_t i;
  int depth;
  const char *err;
} Jp;

typedef struct {
  char *p;
  size_t n;
  size_t cap;
  int depth;
  const char *err;
} Jb;

static locale_t json_c_locale(void) {
  static locale_t loc;
  if (!loc)
    loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
  return loc;
}

static double json_strtod(const char *s, char **end) {
  locale_t loc = json_c_locale();
#ifdef __APPLE__
  if (loc)
    return strtod_l(s, end, loc);
#else
  if (loc) {
    locale_t old = uselocale(loc);
    double x = strtod(s, end);
    uselocale(old);
    return x;
  }
#endif
  return strtod(s, end);
}

static int json_fmt_double(char *tmp, size_t n, double x) {
  locale_t loc = json_c_locale();
#ifdef __APPLE__
  if (loc)
    return snprintf_l(tmp, n, loc, "%.17g", x);
#else
  if (loc) {
    locale_t old = uselocale(loc);
    int r = snprintf(tmp, n, "%.17g", x);
    uselocale(old);
    return r;
  }
#endif
  return snprintf(tmp, n, "%.17g", x);
}

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

static SzAdt *result_ok(void *payload) { return json_mk(RESULT_OK, payload); }

static SzAdt *result_err(const char *msg) {
  SzString *s = sz_string_from_cstr(msg ? msg : "error");
  return json_mk(RESULT_ERR, s);
}

static SzAdt *json_null(void) { return sz_adt_new(JSON_NULL, NULL); }

static SzAdt *json_bool(int v) { return json_mk(JSON_BOOL, sz_box_i64(v ? 1 : 0)); }

static SzAdt *json_int(int64_t n) { return json_mk(JSON_INT, sz_box_i64(n)); }

static SzAdt *json_float(double x) { return json_mk(JSON_FLOAT, box_f64(x)); }

static SzAdt *json_str(SzString *s) { return json_mk(JSON_STR, s); }

static void buf_grow(char **p, size_t n, size_t *cap, size_t need) {
  size_t nc;
  char *nb;
  if (need <= *cap)
    return;
  nc = *cap ? *cap * 2 : 64;
  while (nc < need)
    nc *= 2;
  nb = (char *)sz_alloc(nc);
  if (*p && n)
    memcpy(nb, *p, n);
  sz_free(*p);
  *p = nb;
  *cap = nc;
}

static void jp_fail(Jp *p, const char *msg) {
  if (p && !p->err)
    p->err = msg;
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
  if (jp_at(p) != (unsigned char)c) {
    jp_fail(p, "Json.parse: unexpected token");
    return;
  }
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

static int jp_hex4(Jp *p, unsigned *cp) {
  int i;
  unsigned v = 0;
  for (i = 0; i < 4; i++) {
    int h;
    if (p->i >= p->n) {
      jp_fail(p, "Json.parse: bad unicode escape");
      return 0;
    }
    h = hex_val(p->s[p->i++]);
    if (h < 0) {
      jp_fail(p, "Json.parse: bad unicode escape");
      return 0;
    }
    v = (v << 4) | (unsigned)h;
  }
  *cp = v;
  return 1;
}

static void jp_room(char **buf, size_t *cap, size_t n, size_t extra) {
  buf_grow(buf, n, cap, n + extra + 1);
}

static SzString *jp_str_fail(Jp *p, char *buf, const char *msg) {
  if (msg)
    jp_fail(p, msg);
  sz_free(buf);
  return NULL;
}

static SzString *jp_string(Jp *p) {
  char *buf;
  size_t cap = 16;
  size_t n = 0;
  jp_skip(p);
  if (jp_at(p) != '"') {
    jp_fail(p, "Json.parse: expected string");
    return NULL;
  }
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
        return jp_str_fail(p, buf, "Json.parse: bad string escape");
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
        unsigned cp = 0;
        unsigned char u[4];
        int nbytes;
        if (!jp_hex4(p, &cp))
          return jp_str_fail(p, buf, NULL);
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          unsigned low = 0;
          if (!(p->i + 2 <= p->n && p->s[p->i] == '\\' && p->s[p->i + 1] == 'u'))
            return jp_str_fail(p, buf, "Json.parse: unpaired surrogate");
          p->i += 2;
          if (!jp_hex4(p, &low))
            return jp_str_fail(p, buf, NULL);
          if (low < 0xDC00 || low > 0xDFFF)
            return jp_str_fail(p, buf, "Json.parse: unpaired surrogate");
          cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          return jp_str_fail(p, buf, "Json.parse: unpaired surrogate");
        }
        if (cp < 0x80) {
          c = (unsigned char)cp;
          break;
        }
        if (cp < 0x800) {
          nbytes = 2;
          u[0] = (unsigned char)(0xC0 | (cp >> 6));
          u[1] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          nbytes = 3;
          u[0] = (unsigned char)(0xE0 | (cp >> 12));
          u[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
          u[2] = (unsigned char)(0x80 | (cp & 0x3F));
        } else {
          nbytes = 4;
          u[0] = (unsigned char)(0xF0 | (cp >> 18));
          u[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
          u[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
          u[3] = (unsigned char)(0x80 | (cp & 0x3F));
        }
        jp_room(&buf, &cap, n, (size_t)nbytes);
        memcpy(buf + n, u, (size_t)nbytes);
        n += (size_t)nbytes;
        continue;
      }
      default:
        return jp_str_fail(p, buf, "Json.parse: bad string escape");
      }
    } else if (c < 0x20) {
      return jp_str_fail(p, buf, "Json.parse: control in string");
    }
    jp_room(&buf, &cap, n, 1);
    buf[n++] = (char)c;
  }
  return jp_str_fail(p, buf, "Json.parse: unterminated string");
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
  if (jp_at(p) < '0' || jp_at(p) > '9') {
    jp_fail(p, "Json.parse: bad number");
    return NULL;
  }
  if (jp_at(p) == '0') {
    p->i++;
  } else {
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  if (jp_at(p) == '.') {
    is_float = 1;
    p->i++;
    if (jp_at(p) < '0' || jp_at(p) > '9') {
      jp_fail(p, "Json.parse: bad number");
      return NULL;
    }
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  if (jp_at(p) == 'e' || jp_at(p) == 'E') {
    is_float = 1;
    p->i++;
    if (jp_at(p) == '+' || jp_at(p) == '-')
      p->i++;
    if (jp_at(p) < '0' || jp_at(p) > '9') {
      jp_fail(p, "Json.parse: bad number");
      return NULL;
    }
    while (jp_at(p) >= '0' && jp_at(p) <= '9')
      p->i++;
  }
  len = p->i - start;
  if (len >= sizeof tmp) {
    jp_fail(p, "Json.parse: number too long");
    return NULL;
  }
  memcpy(tmp, p->s + start, len);
  tmp[len] = '\0';
  if (!is_float) {
    char *end = NULL;
    long long v;
    errno = 0;
    v = strtoll(tmp, &end, 10);
    if (!end || *end != '\0' || errno == ERANGE) {
      jp_fail(p, "Json.parse: integer overflow");
      return NULL;
    }
    return json_int((int64_t)v);
  }
  {
    char *end = NULL;
    double x = json_strtod(tmp, &end);
    if (!end || *end != '\0') {
      jp_fail(p, "Json.parse: bad number");
      return NULL;
    }
    if (!isfinite(x)) {
      jp_fail(p, "Json.parse: non-finite");
      return NULL;
    }
    return json_float(x);
  }
}

static SzAdt *json_rev_list(int32_t tag, SzList *acc, Jp *p) {
  SzList *rev;
  if (p->err) {
    sz_release(acc);
    return NULL;
  }
  rev = sz_list_reverse(acc);
  sz_release(acc);
  return json_mk(tag, rev);
}

static SzAdt *jp_array(Jp *p) {
  SzList *acc;
  jp_eat(p, '[');
  if (p->err)
    return NULL;
  jp_skip(p);
  acc = sz_list_nil();
  if (jp_at(p) != ']') {
    for (;;) {
      SzAdt *v = jp_value(p);
      SzList *old;
      if (p->err) {
        sz_release(v);
        sz_release(acc);
        return NULL;
      }
      old = acc;
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
  return json_rev_list(JSON_ARR, acc, p);
}

static SzAdt *jp_object(Jp *p) {
  SzList *acc;
  jp_eat(p, '{');
  if (p->err)
    return NULL;
  jp_skip(p);
  acc = sz_list_nil();
  if (jp_at(p) != '}') {
    for (;;) {
      SzString *k = jp_string(p);
      SzAdt *v;
      SzPair *ent;
      SzList *old;
      if (p->err) {
        sz_release(k);
        sz_release(acc);
        return NULL;
      }
      jp_eat(p, ':');
      if (p->err) {
        sz_release(k);
        sz_release(acc);
        return NULL;
      }
      v = jp_value(p);
      if (p->err) {
        sz_release(k);
        sz_release(v);
        sz_release(acc);
        return NULL;
      }
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
  return json_rev_list(JSON_OBJ, acc, p);
}

static SzAdt *jp_value(Jp *p) {
  SzAdt *v = NULL;
  jp_skip(p);
  p->depth++;
  if (p->depth > JSON_DEPTH) {
    jp_fail(p, "Json.parse: too deep");
    p->depth--;
    return NULL;
  }
  if (jp_at(p) == '{') {
    v = jp_object(p);
  } else if (jp_at(p) == '[') {
    v = jp_array(p);
  } else if (jp_at(p) == '"') {
    SzString *s = jp_string(p);
    if (!p->err)
      v = json_str(s);
    else
      sz_release(s);
  } else if (jp_at(p) == '-' || (jp_at(p) >= '0' && jp_at(p) <= '9')) {
    v = jp_number(p);
  } else if (jp_starts(p, "true")) {
    p->i += 4;
    v = json_bool(1);
  } else if (jp_starts(p, "false")) {
    p->i += 5;
    v = json_bool(0);
  } else if (jp_starts(p, "null")) {
    p->i += 4;
    v = json_null();
  } else {
    jp_fail(p, "Json.parse: unexpected token");
  }
  p->depth--;
  return v;
}

SzAdt *sz_json_parse(SzString *s) {
  Jp p;
  SzAdt *v;
  if (!s)
    return result_err("Json.parse(null)");
  p.s = sz_string_cstr(s);
  p.n = sz_string_len(s);
  p.i = 0;
  p.depth = 0;
  p.err = NULL;
  v = jp_value(&p);
  if (p.err) {
    sz_release(v);
    return result_err(p.err);
  }
  jp_skip(&p);
  if (p.i != p.n) {
    sz_release(v);
    return result_err("Json.parse: trailing junk");
  }
  return result_ok(v);
}

static void jb_fail(Jb *b, const char *msg) {
  if (b && !b->err)
    b->err = msg;
}

static void jb_put(Jb *b, const char *s, size_t n) {
  if (b->err)
    return;
  buf_grow(&b->p, b->n, &b->cap, b->n + n + 1);
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
  int32_t tag;
  void *pay;
  if (b->err)
    return;
  b->depth++;
  if (b->depth > JSON_DEPTH) {
    jb_fail(b, "Json.stringify: too deep");
    b->depth--;
    return;
  }
  tag = sz_adt_tag(j);
  pay = sz_adt_payload(j);
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
    double x = unbox_f64(pay);
    if (!isfinite(x)) {
      jb_fail(b, "Json.stringify: non-finite");
      break;
    }
    json_fmt_double(tmp, sizeof tmp, x);
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
    jb_fail(b, "Json.stringify: bad tag");
    break;
  }
  b->depth--;
}

SzAdt *sz_json_stringify(SzAdt *j) {
  Jb b;
  SzString *s;
  if (!j)
    return result_err("Json.stringify(null)");
  memset(&b, 0, sizeof b);
  jb_value(&b, j);
  if (b.err) {
    sz_free(b.p);
    return result_err(b.err);
  }
  s = sz_string_from_bytes(b.p ? b.p : "", b.n);
  sz_free(b.p);
  return result_ok(s);
}

static int json_tag(const SzAdt *j) { return j ? sz_adt_tag(j) : -1; }

static SzList *json_obj_list(SzAdt *j) {
  if (json_tag(j) != JSON_OBJ)
    return NULL;
  return (SzList *)sz_adt_payload(j);
}

static SzList *json_arr_list(SzAdt *j) {
  if (json_tag(j) != JSON_ARR)
    return NULL;
  return (SzList *)sz_adt_payload(j);
}

static SzList *json_one(void *v) {
  SzList *xs = sz_list_cons(v, sz_list_nil());
  return xs;
}

static int json_key_eq(SzPair *ent, SzString *key) {
  if (!ent || !key)
    return 0;
  return sz_string_eq((SzString *)sz_pair_left(ent), key);
}

SzList *sz_json_get(SzAdt *j, SzString *key) {
  SzList *xs = json_obj_list(j);
  while (xs && !sz_list_is_empty(xs)) {
    SzPair *ent = (SzPair *)sz_list_head(xs);
    if (json_key_eq(ent, key))
      return json_one(sz_pair_right(ent));
    xs = sz_list_tail(xs);
  }
  return sz_list_nil();
}

SzList *sz_json_keys(SzAdt *j) {
  SzList *xs = json_obj_list(j);
  SzList *acc = NULL;
  SzList *out;
  while (xs && !sz_list_is_empty(xs)) {
    SzPair *ent = (SzPair *)sz_list_head(xs);
    acc = sz_list_cons(sz_pair_left(ent), acc);
    xs = sz_list_tail(xs);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  return out;
}

SzList *sz_json_arr(SzAdt *j) {
  SzList *xs = json_arr_list(j);
  if (!xs)
    return sz_list_nil();
  sz_retain(xs);
  return xs;
}

SzList *sz_json_at(SzAdt *j, int64_t i) {
  SzList *xs = json_arr_list(j);
  int64_t n;
  int64_t k;
  if (!xs || i < 0)
    return sz_list_nil();
  n = sz_list_len(xs);
  if (i >= n)
    return sz_list_nil();
  for (k = 0; k < i; k++)
    xs = sz_list_tail(xs);
  return json_one(sz_list_head(xs));
}

int64_t sz_json_has(SzAdt *j, SzString *key) {
  SzList *g = sz_json_get(j, key);
  int64_t hit = g ? 1 : 0;
  sz_release(g);
  return hit;
}

SzList *sz_json_pairs(SzAdt *j) {
  SzList *xs = json_obj_list(j);
  if (!xs)
    return sz_list_nil();
  sz_retain(xs);
  return xs;
}

int64_t sz_json_is_null(SzAdt *j) { return json_tag(j) == JSON_NULL ? 1 : 0; }
int64_t sz_json_is_bool(SzAdt *j) { return json_tag(j) == JSON_BOOL ? 1 : 0; }
int64_t sz_json_is_int(SzAdt *j) { return json_tag(j) == JSON_INT ? 1 : 0; }
int64_t sz_json_is_float(SzAdt *j) { return json_tag(j) == JSON_FLOAT ? 1 : 0; }
int64_t sz_json_is_str(SzAdt *j) { return json_tag(j) == JSON_STR ? 1 : 0; }
int64_t sz_json_is_arr(SzAdt *j) { return json_tag(j) == JSON_ARR ? 1 : 0; }
int64_t sz_json_is_obj(SzAdt *j) { return json_tag(j) == JSON_OBJ ? 1 : 0; }

static SzList *json_as_payload(SzAdt *j, int tag) {
  if (json_tag(j) != tag)
    return sz_list_nil();
  return json_one(sz_adt_payload(j));
}

SzList *sz_json_as_bool(SzAdt *j) { return json_as_payload(j, JSON_BOOL); }
SzList *sz_json_as_int(SzAdt *j) { return json_as_payload(j, JSON_INT); }
SzList *sz_json_as_float(SzAdt *j) { return json_as_payload(j, JSON_FLOAT); }
SzList *sz_json_as_str(SzAdt *j) { return json_as_payload(j, JSON_STR); }

int64_t sz_json_bool_or(SzAdt *j, int64_t d) {
  SzList *xs = sz_json_as_bool(j);
  int64_t n;
  if (!xs) {
    sz_release(xs);
    return d ? 1 : 0;
  }
  n = sz_unbox_i64(sz_list_head(xs)) ? 1 : 0;
  sz_release(xs);
  return n;
}

int64_t sz_json_int_or(SzAdt *j, int64_t d) {
  SzList *xs = sz_json_as_int(j);
  int64_t n;
  if (!xs) {
    sz_release(xs);
    return d;
  }
  n = sz_unbox_i64(sz_list_head(xs));
  sz_release(xs);
  return n;
}

SzString *sz_json_str_or(SzAdt *j, SzString *d) {
  SzList *xs = sz_json_as_str(j);
  SzString *s;
  if (!xs) {
    sz_release(xs);
    sz_retain(d);
    return d ? d : sz_string_from_cstr("");
  }
  s = (SzString *)sz_list_head(xs);
  sz_retain(s);
  sz_release(xs);
  return s;
}

int64_t sz_json_get_bool(SzAdt *j, SzString *key, int64_t d) {
  SzList *g = sz_json_get(j, key);
  int64_t n;
  if (!g) {
    sz_release(g);
    return d ? 1 : 0;
  }
  n = sz_json_bool_or((SzAdt *)sz_list_head(g), d);
  sz_release(g);
  return n;
}

int64_t sz_json_get_int(SzAdt *j, SzString *key, int64_t d) {
  SzList *g = sz_json_get(j, key);
  int64_t n;
  if (!g) {
    sz_release(g);
    return d;
  }
  n = sz_json_int_or((SzAdt *)sz_list_head(g), d);
  sz_release(g);
  return n;
}

SzString *sz_json_get_str(SzAdt *j, SzString *key, SzString *d) {
  SzList *g = sz_json_get(j, key);
  SzString *s;
  if (!g) {
    sz_release(g);
    sz_retain(d);
    return d ? d : sz_string_from_cstr("");
  }
  s = sz_json_str_or((SzAdt *)sz_list_head(g), d);
  sz_release(g);
  return s;
}

static int json_list_has_key(SzList *xs, SzString *key) {
  while (xs && !sz_list_is_empty(xs)) {
    SzPair *ent = (SzPair *)sz_list_head(xs);
    if (json_key_eq(ent, key))
      return 1;
    xs = sz_list_tail(xs);
  }
  return 0;
}

SzAdt *sz_json_merge(SzAdt *a, SzAdt *b) {
  SzList *left = json_obj_list(a);
  SzList *right = json_obj_list(b);
  SzList *acc = NULL;
  SzList *out;
  SzList *p;
  SzAdt *j;
  if (!left && !right) {
    sz_retain(b);
    return b;
  }
  if (!left) {
    sz_retain(b);
    return b;
  }
  if (!right) {
    sz_retain(a);
    return a;
  }
  for (p = right; p && !sz_list_is_empty(p); p = sz_list_tail(p))
    acc = sz_list_cons(sz_list_head(p), acc);
  for (p = left; p && !sz_list_is_empty(p); p = sz_list_tail(p)) {
    SzPair *ent = (SzPair *)sz_list_head(p);
    SzString *k = ent ? (SzString *)sz_pair_left(ent) : NULL;
    if (!json_list_has_key(right, k))
      acc = sz_list_cons(ent, acc);
  }
  out = sz_list_reverse(acc);
  sz_release(acc);
  j = json_mk(JSON_OBJ, out);
  return j;
}
