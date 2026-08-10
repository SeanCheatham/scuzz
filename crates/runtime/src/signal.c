#include "scalui_ui.h"

#include <string.h>

static char *su_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)su_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

struct SuSignalInt {
  int64_t value;
};

struct SuSignalStr {
  char *value;
};

struct SuSignalList {
  SuList *value;
};

SuSignalInt *su_signal_int(int64_t initial) {
  SuSignalInt *s = (SuSignalInt *)su_alloc(sizeof(SuSignalInt));
  s->value = initial;
  return s;
}

void su_signal_int_set(SuSignalInt *s, int64_t v) {
  if (s)
    s->value = v;
}

int64_t su_signal_int_get(const SuSignalInt *s) { return s ? s->value : 0; }

void su_signal_int_free(SuSignalInt *s) { su_free(s); }

SuSignalStr *su_signal_str(const char *initial) {
  SuSignalStr *s = (SuSignalStr *)su_alloc(sizeof(SuSignalStr));
  s->value = su_strdup(initial);
  return s;
}

void su_signal_str_set(SuSignalStr *s, const char *v) {
  if (!s)
    return;
  su_free(s->value);
  s->value = su_strdup(v);
}

const char *su_signal_str_get(const SuSignalStr *s) {
  return s && s->value ? s->value : "";
}

void su_signal_str_free(SuSignalStr *s) {
  if (!s)
    return;
  su_free(s->value);
  su_free(s);
}

SuSignalList *su_signal_list(SuList *initial) {
  SuSignalList *s = (SuSignalList *)su_alloc(sizeof(SuSignalList));
  s->value = initial;
  return s;
}

void su_signal_list_set(SuSignalList *s, SuList *v) {
  if (s)
    s->value = v;
}

SuList *su_signal_list_get(const SuSignalList *s) { return s ? s->value : NULL; }

void su_signal_list_free(SuSignalList *s) { su_free(s); }
