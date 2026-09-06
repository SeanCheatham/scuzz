/* Shared UTF-8 walk. One code point is one cell. Measures byte length of
 * the sequence at s. Checks continuation bytes for presence, not validity.
 * Bad input moves one byte so the walk always terminates. */
#ifndef SK_UTF8_H
#define SK_UTF8_H

static inline int sk_utf8_clen(const char *s) {
  unsigned char c;
  if (!s || !s[0])
    return 0;
  c = (unsigned char)s[0];
  if (c < 0x80)
    return 1;
  if ((c & 0xe0) == 0xc0)
    return s[1] ? 2 : 1;
  if ((c & 0xf0) == 0xe0)
    return (s[1] && s[2]) ? 3 : 1;
  if ((c & 0xf8) == 0xf0)
    return (s[1] && s[2] && s[3]) ? 4 : 1;
  return 1;
}

#endif /* SK_UTF8_H */
