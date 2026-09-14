#define _POSIX_C_SOURCE 200809L
#include "scuzz_rt.h"

#include <stddef.h>

/* RFC 4122 UUID version 4 as lowercase hex with hyphens. Uses the blessed
   Random stream so TestRuntime stays hermetic. No OpenSSL. No parse. */

static void fmt_uuid(const unsigned char b[16], char out[37]) {
  static const char lut[] = "0123456789abcdef";
  int bi = 0;
  int i;
  for (i = 0; i < 36; i++) {
    if (i == 8 || i == 13 || i == 18 || i == 23)
      out[i] = '-';
    else {
      unsigned char v = b[bi / 2];
      out[i] = lut[bi % 2 == 0 ? (v >> 4) : (v & 15)];
      bi++;
    }
  }
  out[36] = 0;
}

static void *uuid_v4_thunk(void *env) {
  unsigned char b[16];
  char out[37];
  (void)env;
  sz_timeline_log_cstr("Uuid.v4", "");
  sz_random_fill(b, 16);
  b[6] = (unsigned char)((b[6] & 0x0f) | 0x40);
  b[8] = (unsigned char)((b[8] & 0x3f) | 0x80);
  fmt_uuid(b, out);
  return sz_string_from_bytes(out, 36);
}

SzIo *sz_uuid_v4(void) { return sz_io_delay(uuid_v4_thunk, NULL); }
