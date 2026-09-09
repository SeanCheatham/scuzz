#ifndef SCUZZ_WEB_H
#define SCUZZ_WEB_H
#include "scuzz_ui.h"
void sz_web_stop(void);
void sz_web_start(SzUiSession *session);
void sz_web_present(int width, int height, const uint8_t *rgba);
#endif
