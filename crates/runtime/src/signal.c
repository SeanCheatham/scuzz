#include "scuzz_ui.h"

#include <stdio.h>
#include <string.h>

static char *sz_strdup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    s = "";
  n = strlen(s);
  out = (char *)sz_alloc(n + 1);
  memcpy(out, s, n + 1);
  return out;
}

struct SzSignalInt {
  int64_t value;
};

struct SzSignalStr {
  char *value;
  /* Derived from Signal.int via map (recomputed on get). */
  SzSignalInt *map_src;
  SzSignalMapIntFn map_fn;
  void *map_env;
};

struct SzSignalList {
  SzList *value;
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
  SigReg *r = (SigReg *)sz_alloc_zero(sizeof(SigReg));
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
      sz_free(dead);
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
    nb = (char *)sz_alloc(ncap);
    if (*buf) {
      memcpy(nb, *buf, *len);
      sz_free(*buf);
    }
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, s, n);
  *len += n;
  (*buf)[*len] = '\0';
}

SzString *sz_signal_dump(void) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  char line[512];
  SigReg *r;
  SzString *out;
  dump_append(&buf, &len, &cap, "");
  for (r = g_sig_head; r; r = r->next) {
    switch (r->kind) {
    case SIG_INT:
      snprintf(line, sizeof line, "int[%d] = %lld\n", r->id,
               (long long)sz_signal_int_get((const SzSignalInt *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_STR:
      snprintf(line, sizeof line, "str[%d] = \"%s\"\n", r->id,
               sz_signal_str_get((const SzSignalStr *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_LIST: {
      SzList *p = sz_signal_list_get((const SzSignalList *)r->sig);
      snprintf(line, sizeof line, "list[%d] = [", r->id);
      dump_append(&buf, &len, &cap, line);
      for (; p; p = p->tail) {
        const SzString *s = (const SzString *)p->head;
        snprintf(line, sizeof line, "\"%s\"%s", s ? sz_string_cstr(s) : "",
                 p->tail ? ", " : "");
        dump_append(&buf, &len, &cap, line);
      }
      dump_append(&buf, &len, &cap, "]\n");
      break;
    }
    }
  }
  out = sz_string_from_cstr(buf);
  sz_free(buf);
  return out;
}

int64_t sz_law_signal_int(int64_t id) {
  SigReg *r;
  for (r = g_sig_head; r; r = r->next) {
    if (r->id == (int)id && r->kind == SIG_INT)
      return sz_signal_int_get((const SzSignalInt *)r->sig);
  }
  return 0;
}

SzSignalInt *sz_signal_int(int64_t initial) {
  SzSignalInt *s = (SzSignalInt *)sz_alloc(sizeof(SzSignalInt));
  s->value = initial;
  sig_register(SIG_INT, s);
  return s;
}

void sz_signal_int_set(SzSignalInt *s, int64_t v) {
  if (s)
    s->value = v;
}

int64_t sz_signal_int_get(const SzSignalInt *s) { return s ? s->value : 0; }

void sz_signal_int_free(SzSignalInt *s) {
  if (s)
    sig_unregister(s);
  sz_free(s);
}

SzSignalStr *sz_signal_str(const char *initial) {
  SzSignalStr *s = (SzSignalStr *)sz_alloc_zero(sizeof(SzSignalStr));
  s->value = sz_strdup(initial);
  sig_register(SIG_STR, s);
  return s;
}

void sz_signal_str_set(SzSignalStr *s, const char *v) {
  if (!s || s->map_fn)
    return;
  sz_free(s->value);
  s->value = sz_strdup(v);
}

const char *sz_signal_str_get(const SzSignalStr *s) {
  SzSignalStr *mut;
  if (!s)
    return "";
  if (s->map_fn && s->map_src) {
    SzString *out;
    mut = (SzSignalStr *)s;
    out = s->map_fn(sz_signal_int_get(s->map_src), s->map_env);
    sz_free(mut->value);
    mut->value = sz_strdup(out ? sz_string_cstr(out) : "");
    if (out)
      sz_string_free(out);
  }
  return s->value ? s->value : "";
}

void sz_signal_str_free(SzSignalStr *s) {
  if (!s)
    return;
  sig_unregister(s);
  sz_free(s->value);
  sz_free(s);
}

SzSignalStr *sz_lang_signal_map(SzSignalInt *src, SzSignalMapIntFn fn, void *env) {
  SzSignalStr *s = (SzSignalStr *)sz_alloc_zero(sizeof(SzSignalStr));
  s->map_src = src;
  s->map_fn = fn;
  s->map_env = env;
  s->value = sz_strdup("");
  sig_register(SIG_STR, s);
  /* Prime cached value. */
  (void)sz_signal_str_get(s);
  return s;
}

SzSignalList *sz_signal_list(SzList *initial) {
  SzSignalList *s = (SzSignalList *)sz_alloc(sizeof(SzSignalList));
  s->value = initial;
  sig_register(SIG_LIST, s);
  return s;
}

void sz_signal_list_set(SzSignalList *s, SzList *v) {
  if (s)
    s->value = v;
}

SzList *sz_signal_list_get(const SzSignalList *s) { return s ? s->value : NULL; }

void sz_signal_list_free(SzSignalList *s) {
  if (s)
    sig_unregister(s);
  sz_free(s);
}
