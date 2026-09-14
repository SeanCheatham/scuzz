#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stddef.h>

/* RFC 4648 Base64 of UTF-8 bytes, and the reverse. Software so web and mobile
   compile without OpenSSL. No URL-safe alphabet. No UUID. */

static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(int c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

SzString *sz_base64_encode(const SzString *s) {
  const unsigned char *data = (const unsigned char *)(s && s->data ? s->data : "");
  size_t n = s ? s->len : 0;
  size_t groups;
  size_t out_len;
  char *buf;
  SzString *out;
  size_t i;
  size_t j = 0;
  if (n > SIZE_MAX - 2)
    return sz_string_from_cstr("");
  groups = (n + 2) / 3;
  if (groups > SIZE_MAX / 4)
    return sz_string_from_cstr("");
  out_len = groups * 4;
  buf = (char *)sz_alloc(out_len + 1);
  for (i = 0; i < n; i += 3) {
    unsigned a = data[i];
    unsigned b = i + 1 < n ? data[i + 1] : 0;
    unsigned c = i + 2 < n ? data[i + 2] : 0;
    unsigned t = (a << 16) | (b << 8) | c;
    buf[j++] = alphabet[(t >> 18) & 63];
    buf[j++] = alphabet[(t >> 12) & 63];
    buf[j++] = i + 1 < n ? alphabet[(t >> 6) & 63] : '=';
    buf[j++] = i + 2 < n ? alphabet[t & 63] : '=';
  }
  buf[out_len] = 0;
  out = sz_string_from_bytes(buf, out_len);
  sz_free(buf);
  return out;
}

SzString *sz_base64_decode(const SzString *s) {
  const unsigned char *data = (const unsigned char *)(s && s->data ? s->data : "");
  size_t n = s ? s->len : 0;
  size_t pad = 0;
  size_t i;
  size_t out_len;
  unsigned char *raw;
  SzString *out;
  if (n == 0)
    return sz_string_from_cstr("");
  if (n % 4 != 0)
    return sz_string_from_cstr("");
  if (data[n - 1] == '=')
    pad++;
  if (pad && data[n - 2] == '=')
    pad++;
  for (i = 0; i < n - pad; i++) {
    if (b64_val((int)data[i]) < 0)
      return sz_string_from_cstr("");
  }
  for (i = n - pad; i < n; i++) {
    if (data[i] != '=')
      return sz_string_from_cstr("");
  }
  out_len = (n / 4) * 3 - pad;
  if (out_len == 0)
    return sz_string_from_cstr("");
  raw = (unsigned char *)sz_alloc(out_len);
  for (i = 0; i < n; i += 4) {
    unsigned v0 = (unsigned)b64_val((int)data[i]);
    unsigned v1 = (unsigned)b64_val((int)data[i + 1]);
    unsigned v2 = data[i + 2] == '=' ? 0 : (unsigned)b64_val((int)data[i + 2]);
    unsigned v3 = data[i + 3] == '=' ? 0 : (unsigned)b64_val((int)data[i + 3]);
    unsigned t = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
    size_t o = (i / 4) * 3;
    raw[o] = (unsigned char)(t >> 16);
    if (o + 1 < out_len)
      raw[o + 1] = (unsigned char)(t >> 8);
    if (o + 2 < out_len)
      raw[o + 2] = (unsigned char)t;
  }
  out = sz_string_from_bytes((const char *)raw, out_len);
  sz_free(raw);
  return out;
}
