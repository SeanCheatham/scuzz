#ifndef SCUZZ_NATIVE_TEXT_H
#define SCUZZ_NATIVE_TEXT_H

#include "../include/sk_text.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float sk_native_text_measure(SkTextRange range, float size, int style);
void sk_native_text_positions(SkTextRange range, float size, int style, float *leading, float *trailing);
void sk_native_text_breaks(const char *text, size_t bytes, uint8_t *breaks);
void sk_native_text_metrics(float size, int style, float *ascent, float *descent);
void sk_native_text_span_metrics(SkTextRange range, float size, int style,
                                 float *ascent, float *descent);

/* Coverage rows use device coordinates inside the supplied clip. */
typedef void (*SkTextCoverage)(void *data, int x, int y, int count, const uint8_t *coverage);
void sk_native_text_draw(SkTextRange range, float size, int style,
                         double x, double y, double scale,
                         int x0, int y0, int x1, int y1,
                         SkTextCoverage coverage, void *data);

#ifdef __cplusplus
}
#endif

#endif
