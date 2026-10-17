#ifndef SCUZZ_IMAGE_H
#define SCUZZ_IMAGE_H

#include "scuzz_ui.h"
#include <stdatomic.h>

struct SzImage {
  int width, height;
  _Atomic(void *) bitmap;
  _Atomic(void (*)(void *)) drop_bitmap;
  uint8_t pixels[];
};

#endif
