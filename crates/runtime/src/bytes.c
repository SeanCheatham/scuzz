#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stddef.h>

/* Copy UTF-8 / raw bytes from a String into a Bytes value. Software so web
   and mobile compile without OpenSSL. No toHex. No toStr. */

SzString *sz_bytes_from_str(const SzString *s) {
  const char *data = s && s->data ? s->data : "";
  size_t n = s ? (size_t)s->len : 0;
  return sz_string_from_bytes(data, n);
}
