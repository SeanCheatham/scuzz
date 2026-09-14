#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stddef.h>

/* Lowercase hex of UTF-8 bytes, and the reverse. Software so web and mobile
   compile without OpenSSL. No base64. No UUID. */

static void hex_lower(const unsigned char *in, size_t n, char *out) {
  static const char lut[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < n; i++) {
    out[i * 2] = lut[in[i] >> 4];
    out[i * 2 + 1] = lut[in[i] & 15];
  }
}

static int nibble(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

SzString *sz_hex_encode(const SzString *s) {
  const unsigned char *data = (const unsigned char *)(s && s->data ? s->data : "");
  size_t n = s ? s->len : 0;
  char *hex;
  SzString *out;
  if (n > (SIZE_MAX - 1) / 2)
    return sz_string_from_cstr("");
  hex = (char *)sz_alloc(n * 2 + 1);
  hex_lower(data, n, hex);
  hex[n * 2] = 0;
  out = sz_string_from_bytes(hex, n * 2);
  sz_free(hex);
  return out;
}

SzString *sz_hex_decode(const SzString *s) {
  const unsigned char *data = (const unsigned char *)(s && s->data ? s->data : "");
  size_t n = s ? s->len : 0;
  unsigned char *raw;
  SzString *out;
  size_t i;
  if (n % 2 != 0)
    return sz_string_from_cstr("");
  if (n == 0)
    return sz_string_from_cstr("");
  raw = (unsigned char *)sz_alloc(n / 2);
  for (i = 0; i < n; i += 2) {
    int hi = nibble((int)data[i]);
    int lo = nibble((int)data[i + 1]);
    if (hi < 0 || lo < 0) {
      sz_free(raw);
      return sz_string_from_cstr("");
    }
    raw[i / 2] = (unsigned char)((hi << 4) | lo);
  }
  out = sz_string_from_bytes((const char *)raw, n / 2);
  sz_free(raw);
  return out;
}
