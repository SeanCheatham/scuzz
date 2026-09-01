#include "scuzz_ui.h"

#include "rt_util.h"

#include <stdio.h>
#include <string.h>

struct SzSignalInt {
  int64_t value;
};

struct SzSignalStr {
  char *value;
  /* Derived from Signal.int through map (recomputed on get). */
  SzSignalInt *map_src;
  SzSignalMapIntFn map_fn;
  void *map_env;
};

struct SzSignalList {
  SzList *value;
};

/* --- signal store registry (fuzz / dump oracle) --------------------------- */
/* Every live signal, in creation order. Ids are monotonic. A dump stays
   stable across frees. */

typedef enum { SIG_INT = 1, SIG_STR = 2, SIG_LIST = 3 } SigKind;

typedef struct SigReg {
  SigKind kind;
  int id;
  char *name;
  int elem_str;
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
  r->name = sz_strdup("");
  r->elem_str = 1;
  r->sig = sig;
  if (g_sig_tail)
    g_sig_tail->next = r;
  else
    g_sig_head = r;
  g_sig_tail = r;
}

/* Publish the author-facing name of a signal (its `for` binder name). */
void sz_signal_name(const void *sig, const char *name) {
  SigReg *r;
  for (r = g_sig_head; r; r = r->next) {
    if (r->sig == sig) {
      sz_free(r->name);
      r->name = sz_strdup(name);
      return;
    }
  }
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
      sz_free(dead->name);
      sz_free(dead);
      return;
    }
    p = &(*p)->next;
  }
}

static SigReg *sig_find(SigKind kind, const char *name) {
  SigReg *r;
  if (!name || !name[0])
    return NULL;
  for (r = g_sig_head; r; r = r->next) {
    if (r->kind == kind && strcmp(r->name, name) == 0)
      return r;
  }
  return NULL;
}

/* Mark a list signal's element kind: 1 = String (dump prints elements),
 * 0 = other (dump prints the count only). */
static void sig_set_elem_str(const void *sig, int64_t elem_str) {
  SigReg *r;
  for (r = g_sig_head; r; r = r->next) {
    if (r->sig == sig) {
      r->elem_str = elem_str ? 1 : 0;
      return;
    }
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
  char tag[256];
  SigReg *r;
  SzString *out;
  dump_append(&buf, &len, &cap, "");
  for (r = g_sig_head; r; r = r->next) {
    if (r->name && r->name[0])
      snprintf(tag, sizeof tag, "%s ", r->name);
    else
      tag[0] = '\0';
    switch (r->kind) {
    case SIG_INT:
      snprintf(line, sizeof line, "int[%d] %s= %lld\n", r->id, tag,
               (long long)sz_signal_int_get((const SzSignalInt *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_STR:
      snprintf(line, sizeof line, "str[%d] %s= \"%s\"\n", r->id, tag,
               sz_signal_str_get((const SzSignalStr *)r->sig));
      dump_append(&buf, &len, &cap, line);
      break;
    case SIG_LIST: {
      SzList *p = sz_signal_list_get((const SzSignalList *)r->sig);
      if (!r->elem_str) {
        snprintf(line, sizeof line, "list[%d] %s= <%lld>\n", r->id, tag,
                 (long long)sz_list_len(p));
        dump_append(&buf, &len, &cap, line);
        break;
      }
      snprintf(line, sizeof line, "list[%d] %s= [", r->id, tag);
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

int64_t sz_property_signal_int(SzString *name) {
  const char *n = name ? sz_string_cstr(name) : "";
  SigReg *r;
  if (sz_timeline_replaying())
    return sz_timeline_replay_signal_int(n);
  r = sig_find(SIG_INT, n);
  return r ? sz_signal_int_get((const SzSignalInt *)r->sig) : 0;
}

SzString *sz_property_signal_str(SzString *name) {
  SigReg *r = sig_find(SIG_STR, name ? sz_string_cstr(name) : "");
  if (r)
    return sz_string_from_cstr(
        sz_signal_str_get((const SzSignalStr *)r->sig));
  return sz_string_from_cstr("");
}

int64_t sz_property_signal_list_len(SzString *name) {
  SigReg *r = sig_find(SIG_LIST, name ? sz_string_cstr(name) : "");
  if (r)
    return (int64_t)sz_list_len(
        sz_signal_list_get((const SzSignalList *)r->sig));
  return 0;
}

SzString *sz_property_signal_list_at(SzString *name, int64_t index) {
  SigReg *r;
  const SzList *p;
  int64_t i;
  if (index < 0)
    return sz_string_from_cstr("");
  r = sig_find(SIG_LIST, name ? sz_string_cstr(name) : "");
  if (r && r->elem_str) {
    p = sz_signal_list_get((const SzSignalList *)r->sig);
    i = 0;
    while (p) {
      if (i == index) {
        SzString *h = (SzString *)p->head;
        return sz_string_from_cstr(h ? sz_string_cstr(h) : "");
      }
      p = p->tail;
      i++;
    }
  }
  return sz_string_from_cstr("");
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
  sz_release(s->map_env);
  s->map_env = NULL;
  sz_free(s);
}

/* --- Lang-facing wrappers (kept here so Signal use does not drag in ui.o) -- */

SzSignalInt *sz_lang_signal_int(int64_t initial, SzString *name) {
  SzSignalInt *s = sz_signal_int(initial);
  if (name)
    sz_signal_name(s, sz_string_cstr(name));
  return s;
}

int64_t sz_lang_signal_get(SzSignalInt *s) { return sz_signal_int_get(s); }

void *sz_lang_signal_set(SzSignalInt *s, int64_t v) {
  sz_signal_int_set(s, v);
  return NULL;
}

SzSignalStr *sz_lang_signal_str(SzString *initial, SzString *name) {
  SzSignalStr *s = sz_signal_str(initial ? sz_string_cstr(initial) : "");
  if (name)
    sz_signal_name(s, sz_string_cstr(name));
  return s;
}

SzString *sz_lang_signal_str_get(SzSignalStr *s) {
  return sz_string_from_cstr(sz_signal_str_get(s));
}

void *sz_lang_signal_str_set(SzSignalStr *s, SzString *v) {
  sz_signal_str_set(s, v ? sz_string_cstr(v) : "");
  return NULL;
}

int sz_signal_list_elem_str(const SzSignalList *s) {
  SigReg *r;
  for (r = g_sig_head; r; r = r->next) {
    if (r->sig == (const void *)s)
      return r->elem_str;
  }
  return 1;
}

SzSignalList *sz_lang_signal_list(SzList *initial, SzString *name,
                                  int64_t elem_str) {
  SzSignalList *s = sz_signal_list(initial);
  sig_set_elem_str(s, elem_str);
  if (name)
    sz_signal_name(s, sz_string_cstr(name));
  return s;
}

SzList *sz_lang_signal_list_get(SzSignalList *s) { return sz_signal_list_get(s); }

void *sz_lang_signal_list_set(SzSignalList *s, SzList *v) {
  sz_signal_list_set(s, v);
  return NULL;
}

SzSignalStr *sz_lang_signal_map(SzSignalInt *src, SzSignalMapIntFn fn,
                                void *env, SzString *name) {
  SzSignalStr *s = (SzSignalStr *)sz_alloc_zero(sizeof(SzSignalStr));
  s->map_src = src;
  s->map_fn = fn;
  sz_retain(env);
  s->map_env = env;
  s->value = sz_strdup("");
  sig_register(SIG_STR, s);
  if (name)
    sz_signal_name(s, sz_string_cstr(name));
  /* Prime cached value. */
  (void)sz_signal_str_get(s);
  return s;
}

SzSignalList *sz_signal_list(SzList *initial) {
  SzSignalList *s = (SzSignalList *)sz_alloc(sizeof(SzSignalList));
  sz_retain(initial);
  s->value = initial;
  sig_register(SIG_LIST, s);
  return s;
}

void sz_signal_list_set(SzSignalList *s, SzList *v) {
  if (!s)
    return;
  if (s->value == v)
    return;
  sz_release(s->value);
  sz_retain(v);
  s->value = v;
}

SzList *sz_signal_list_get(const SzSignalList *s) { return s ? s->value : NULL; }

void sz_signal_list_free(SzSignalList *s) {
  if (!s)
    return;
  sig_unregister(s);
  sz_list_free(s->value);
  sz_free(s);
}
