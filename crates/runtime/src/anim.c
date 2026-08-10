#include "scalui_ui.h"

#include <string.h>

/* Animation: float lerp advanced on pump via Clock dt. */

struct SuAnimFloat {
  float from;
  float to;
  float current;
  int64_t duration_ms;
  int64_t elapsed_ms;
  int done;
  SuAnimFloat *next; /* session registry */
};

static SuAnimFloat *g_anims = NULL;

SuAnimFloat *su_anim_float(float from, float to, int64_t duration_ms) {
  SuAnimFloat *a = (SuAnimFloat *)su_alloc_zero(sizeof(SuAnimFloat));
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

void su_anim_free(SuAnimFloat *a) {
  SuAnimFloat **pp;
  if (!a)
    return;
  for (pp = &g_anims; *pp; pp = &(*pp)->next) {
    if (*pp == a) {
      *pp = a->next;
      break;
    }
  }
  su_free(a);
}

float su_anim_value(const SuAnimFloat *a) { return a ? a->current : 0.f; }

int su_anim_done(const SuAnimFloat *a) { return a ? a->done : 1; }

void su_anim_tick(SuAnimFloat *a, int64_t dt_ms) {
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

void su_anim_tick_all(int64_t dt_ms) {
  SuAnimFloat *a;
  for (a = g_anims; a; a = a->next)
    su_anim_tick(a, dt_ms);
}
