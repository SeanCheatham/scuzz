#include "scalui_ui.h"

#include <stdio.h>
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

/* --- signal store registry (fuzz / dump oracle) --------------------------- */
/* Every live signal, in creation order. Ids are monotonic so a dump stays
   stable across frees. */

typedef enum { SIG_INT = 1, SIG_STR = 2, SIG_LIST = 3 } SigKind;

typedef struct SigReg {
  SigKind kind;
  int id;
  const void *sig;
  struct SigReg *next;
} SigReg;

static SigReg *g_sig_head = NULL;
static SigReg *g_sig_tail = NULL;
static int g_sig_next_id = 0;

static void sig_register(SigKind kind, const void *sig) {
  SigReg *r = (SigReg *)su_alloc_zero(sizeof(SigReg));
  r->kind = kind;
  r->id = g_sig_next_id++;
  r->sig = sig;
  if (g_sig_tail)
    g_sig_tail->next = r;
  else
    g_sig_head = r;
  g_sig_tail = r;
}

static void sig_unregister(const void *sig) {
  SigReg **p = &g_sig_head;
  while (*p) {
    if ((*p)->sig == sig) {
      SigReg *dead = *p;
      *p = dead->next;
      if (g_sig_tail == dead) {
        g_sig_tail = g_sig_head;
        while (g_sig_tail && g_sig_tail->next)
          g_sig_tail = g_sig_tail->next;
      }
      su_free(dead);
      return;
    }
    p = &(*p)->next;
  }
}

static void dump_append(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t n = strlen(s);
  if (*len + n + 1 > *cap) {
    size_t ncap = *cap ? *cap : 256;
    char *nb;
    while (*len + n + 1 > ncap)
      ncap *= 2;
    nb = (char *)su_alloc(ncap);
    if (*buf) {
      memcpy(nb, *buf, *len);
      su_free(*buf);
    }
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}

SuString *su_signal_dump(void) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  char line[512];
  SigReg *r;
  SuString *out;
  dump_append(&buf, &len, &cap, "");
  for (r = g_sig_head; r; r = r->next) {
    switch (r->kind) {
    case SIG_INT:
      snprintf(line, sizeof line, "int[%d] = %lld\n", r->id,
               (long long)su_signal_int_get((const SuSignalInt *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_STR:
      snprintf(line, sizeof line, "str[%d] = \"%s\"\n", r->id,
               su_signal_str_get((const SuSignalStr *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_LIST: {
      SuList *p = su_signal_list_get((const SuSignalList *)r->sig);
      snprintf(line, sizeof line, "list[%d] = [", r->id);
      dump_append(&buf, &len, &cap, line);
      for (; p; p = p->tail) {
        const SuString *s = (const SuString *)p->head;
        snprintf(line, sizeof line, "\"%s\"%s", s ? su_string_cstr(s) : "",
                 p->tail ? ", " : "");
        dump_append(&buf, &len, &cap, line);
      }
      dump_append(&buf, &len, &cap, "]\n");
      break;
    }
    }
  }
  out = su_string_from_cstr(buf);
  su_free(buf);
  return out;
}

SuSignalInt *su_signal_int(int64_t initial) {
  SuSignalInt *s = (SuSignalInt *)su_alloc(sizeof(SuSignalInt));
  s->value = initial;
  sig_register(SIG_INT, s);
  return s;
}

void su_signal_int_set(SuSignalInt *s, int64_t v) {
  if (s)
    s->value = v;
}

int64_t su_signal_int_get(const SuSignalInt *s) { return s ? s->value : 0; }

void su_signal_int_free(SuSignalInt *s) {
  if (s)
    sig_unregister(s);
  su_free(s);
}

SuSignalStr *su_signal_str(const char *initial) {
  SuSignalStr *s = (SuSignalStr *)su_alloc(sizeof(SuSignalStr));
  s->value = su_strdup(initial);
  sig_register(SIG_STR, s);
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
  sig_unregister(s);
  su_free(s->value);
  su_free(s);
}

SuSignalList *su_signal_list(SuList *initial) {
  SuSignalList *s = (SuSignalList *)su_alloc(sizeof(SuSignalList));
  s->value = initial;
  sig_register(SIG_LIST, s);
  return s;
}

void su_signal_list_set(SuSignalList *s, SuList *v) {
  if (s)
    s->value = v;
}

SuList *su_signal_list_get(const SuSignalList *s) { return s ? s->value : NULL; }

void su_signal_list_free(SuSignalList *s) {
  if (s)
    sig_unregister(s);
  su_free(s);
}
