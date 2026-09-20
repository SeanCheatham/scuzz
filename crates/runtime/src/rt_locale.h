#ifndef SCUZZ_RT_LOCALE_H
#define SCUZZ_RT_LOCALE_H

/* Needs _POSIX_C_SOURCE 200809L (or better) before the first include. */
#include <locale.h>

/* Cached C locale for locale-independent number parse and format. */
static inline locale_t rt_c_locale(void) {
  static locale_t loc;
  if (!loc)
    loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
  return loc;
}

#endif
