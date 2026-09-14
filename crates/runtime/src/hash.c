#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* SHA-256 of UTF-8 bytes. Lowercase hex. Software so web and mobile compile
   without OpenSSL. No HMAC. No other digests. */

static void hex_lower(const unsigned char *in, size_t n, char *out) {
  static const char lut[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < n; i++) {
    out[i * 2] = lut[in[i] >> 4];
    out[i * 2 + 1] = lut[in[i] & 15];
  }
  out[n * 2] = 0;
}

static uint32_t rotr32(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}

static uint32_t load_be32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

static void store_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void store_be64(unsigned char *p, uint64_t v) {
  store_be32(p, (uint32_t)(v >> 32));
  store_be32(p + 4, (uint32_t)v);
}

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

static void sha256_compress(uint32_t st[8], const unsigned char block[64]) {
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  unsigned i;
  for (i = 0; i < 16; i++)
    w[i] = load_be32(block + i * 4);
  for (i = 16; i < 64; i++) {
    uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = st[0];
  b = st[1];
  c = st[2];
  d = st[3];
  e = st[4];
  f = st[5];
  g = st[6];
  h = st[7];
  for (i = 0; i < 64; i++) {
    uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t t1 = h + s1 + ch + K[i] + w[i];
    uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  st[0] += a;
  st[1] += b;
  st[2] += c;
  st[3] += d;
  st[4] += e;
  st[5] += f;
  st[6] += g;
  st[7] += h;
}

static void sha256(const unsigned char *data, size_t n, unsigned char out[32]) {
  uint32_t st[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  unsigned char block[64];
  unsigned i;
  uint64_t bits = (uint64_t)n * 8u;
  while (n >= 64) {
    sha256_compress(st, data);
    data += 64;
    n -= 64;
  }
  memcpy(block, data, n);
  block[n] = 0x80;
  if (n + 9 <= 64) {
    memset(block + n + 1, 0, 55 - n);
    store_be64(block + 56, bits);
    sha256_compress(st, block);
  } else {
    memset(block + n + 1, 0, 63 - n);
    sha256_compress(st, block);
    memset(block, 0, 56);
    store_be64(block + 56, bits);
    sha256_compress(st, block);
  }
  for (i = 0; i < 8; i++)
    store_be32(out + i * 4, st[i]);
}

SzString *sz_hash_sha256(const SzString *s) {
  unsigned char dig[32];
  char hex[65];
  const unsigned char *data = (const unsigned char *)(s && s->data ? s->data : "");
  size_t n = s ? s->len : 0;
  sha256(data, n, dig);
  hex_lower(dig, 32, hex);
  return sz_string_from_cstr(hex);
}
