#include "scuzz_ui.h"

#include <string.h>

/* Animation: float lerp advanced on pump via Clock dt. */

struct SzAnimFloat {
  float from;
  float to;
  float current;
  int64_t duration_ms;
  int64_t elapsed_ms;
  int done;
  SzAnimFloat *next; /* session registry */
};

static SzAnimFloat *g_anims = NULL;

SzAnimFloat *sz_anim_float(float from, float to, int64_t duration_ms) {
  SzAnimFloat *a = (SzAnimFloat *)sz_alloc_zero(sizeof(SzAnimFloat));
  a->from = from;
  a->to = to;
  a->current = from;
  a->duration_ms = duration_ms < 0 ? 0 : duration_ms;
  a->elapsed_ms = 0;
  a->done = duration_ms <= 0 ? 1 : 0;
  if (a->done)
    a->current = to;
  a->next = g_anims;
  g_anims = a;
  return a;
}

void sz_anim_free(SzAnimFloat *a) {
  SzAnimFloat **pp;
  if (!a)
    return;
  for (pp = &g_anims; *pp; pp = &(*pp)->next) {
    if (*pp == a) {
      *pp = a->next;
      break;
    }
  }
  sz_free(a);
}

float sz_anim_value(const SzAnimFloat *a) { return a ? a->current : 0.f; }

int sz_anim_done(const SzAnimFloat *a) { return a ? a->done : 1; }

void sz_anim_tick(SzAnimFloat *a, int64_t dt_ms) {
  float t;
  if (!a || a->done || dt_ms <= 0)
    return;
  a->elapsed_ms += dt_ms;
  if (a->duration_ms <= 0 || a->elapsed_ms >= a->duration_ms) {
    a->current = a->to;
    a->done = 1;
    return;
  }
  t = (float)a->elapsed_ms / (float)a->duration_ms;
  a->current = a->from + (a->to - a->from) * t;
}

void sz_anim_tick_all(int64_t dt_ms) {
  SzAnimFloat *a;
  for (a = g_anims; a; a = a->next)
    sz_anim_tick(a, dt_ms);
}
