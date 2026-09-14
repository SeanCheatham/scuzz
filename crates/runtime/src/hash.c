#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <openssl/evp.h>
#include <stddef.h>

/* SHA-256 of UTF-8 bytes. Lowercase hex. No HMAC. No other digests. */

static void hex_lower(const unsigned char *in, size_t n, char *out) {
  static const char lut[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < n; i++) {
    out[i * 2] = lut[in[i] >> 4];
    out[i * 2 + 1] = lut[in[i] & 15];
  }
  out[n * 2] = 0;
}

SzString *sz_hash_sha256(const SzString *s) {
  unsigned char dig[32];
  unsigned int len = 0;
  char hex[65];
  const unsigned char *data;
  size_t n;
  EVP_MD_CTX *ctx;

  data = (const unsigned char *)(s && s->data ? s->data : "");
  n = s ? s->len : 0;
  ctx = EVP_MD_CTX_new();
  if (!ctx)
    return sz_string_from_cstr("");
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, data, n) != 1 ||
      EVP_DigestFinal_ex(ctx, dig, &len) != 1 || len != 32) {
    EVP_MD_CTX_free(ctx);
    return sz_string_from_cstr("");
  }
  EVP_MD_CTX_free(ctx);
  hex_lower(dig, 32, hex);
  return sz_string_from_cstr(hex);
}
