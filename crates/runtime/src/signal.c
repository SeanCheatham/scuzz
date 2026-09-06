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

/* Publish the author-facing name of a signal (its `for` binder name).
 * Last non-empty name wins: a later bind clears the same name on other
 * signals of the same kind. An unregistered signal clears nothing. */
void sz_signal_name(const void *sig, const char *name) {
  SigReg *r;
  SigReg *mine = NULL;
  const char *n = name ? name : "";
  for (r = g_sig_head; r; r = r->next) {
    if (r->sig == sig) {
      mine = r;
      break;
    }
  }
  if (!mine)
    return;
  if (n[0]) {
    for (r = g_sig_head; r; r = r->next) {
      if (r != mine && r->kind == mine->kind && r->name &&
          strcmp(r->name, n) == 0) {
        sz_free(r->name);
        r->name = sz_strdup("");
      }
    }
  }
  sz_free(mine->name);
  mine->name = sz_strdup(n);
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

static void sig_missing(const char *name) {
  char buf[192];
  snprintf(buf, sizeof buf, "missing signal %s",
           name && name[0] ? name : "(empty)");
  sz_panic(buf);
}

/* First non-null head decides String vs count-only. Empty keeps `unknown`. */
static int sig_list_heads_str(const SzList *p, int unknown) {
  for (; p; p = p->tail) {
    if (!p->head)
      continue;
    return sz_rc_kind(p->head) == SZ_RC_STRING;
  }
  return unknown ? 1 : 0;
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

/* 1 when the head is a String the dump may print, 0 otherwise. */
static int sig_head_str(const void *head) {
  return head && sz_rc_kind(head) == SZ_RC_STRING;
}

SzString *sz_signal_dump(void) {
  char *buf = NULL;
  size_t len = 0, cap = 0;
  char num[32];
  SigReg *r;
  SzString *out;
  sz_dump_append(&buf, &len, &cap, "");
  for (r = g_sig_head; r; r = r->next) {
    sz_dump_append(&buf, &len, &cap,
                   r->kind == SIG_INT ? "int" : r->kind == SIG_STR ? "str"
                                                                    : "list");
    snprintf(num, sizeof num, "[%d] ", r->id);
    sz_dump_append(&buf, &len, &cap, num);
    if (r->name && r->name[0]) {
      sz_dump_append(&buf, &len, &cap, r->name);
      sz_dump_append(&buf, &len, &cap, " ");
    }
    sz_dump_append(&buf, &len, &cap, "= ");
    switch (r->kind) {
    case SIG_INT:
      snprintf(num, sizeof num, "%lld\n",
               (long long)sz_signal_int_get((const SzSignalInt *)r->sig));
      sz_dump_append(&buf, &len, &cap, num);
      break;
    case SIG_STR:
      sz_dump_append(&buf, &len, &cap, "\"");
      sz_dump_append_escaped(&buf, &len, &cap,
                             sz_signal_str_get((const SzSignalStr *)r->sig));
      sz_dump_append(&buf, &len, &cap, "\"\n");
      break;
    case SIG_LIST: {
      SzList *p = sz_signal_list_get((const SzSignalList *)r->sig);
      if (!sig_list_heads_str(p, r->elem_str)) {
        snprintf(num, sizeof num, "<%lld>\n", (long long)sz_list_len(p));
        sz_dump_append(&buf, &len, &cap, num);
        break;
      }
      sz_dump_append(&buf, &len, &cap, "[");
      for (; p; p = p->tail) {
        sz_dump_append(&buf, &len, &cap, "\"");
        sz_dump_append_escaped(&buf, &len, &cap,
                               sig_head_str(p->head)
                                   ? sz_string_cstr((const SzString *)p->head)
                                   : "");
        sz_dump_append(&buf, &len, &cap, p->tail ? "\", " : "\"");
      }
      sz_dump_append(&buf, &len, &cap, "]\n");
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
  if (!r)
    sig_missing(n);
  return sz_signal_int_get((const SzSignalInt *)r->sig);
}

SzString *sz_property_signal_str(SzString *name) {
  const char *n = name ? sz_string_cstr(name) : "";
  SigReg *r;
  if (sz_timeline_replaying())
    return sz_timeline_replay_signal_str(n);
  r = sig_find(SIG_STR, n);
  if (!r)
    sig_missing(n);
  return sz_string_from_cstr(sz_signal_str_get((const SzSignalStr *)r->sig));
}

int64_t sz_property_signal_list_len(SzString *name) {
  const char *n = name ? sz_string_cstr(name) : "";
  SigReg *r;
  if (sz_timeline_replaying())
    return sz_timeline_replay_signal_list_len(n);
  r = sig_find(SIG_LIST, n);
  if (!r)
    sig_missing(n);
  return (int64_t)sz_list_len(sz_signal_list_get((const SzSignalList *)r->sig));
}

SzString *sz_property_signal_list_at(SzString *name, int64_t index) {
  SigReg *r;
  const SzList *p;
  int64_t i;
  const char *n = name ? sz_string_cstr(name) : "";
  if (index < 0)
    return sz_string_from_cstr("");
  if (sz_timeline_replaying())
    return sz_timeline_replay_signal_list_at(n, index);
  r = sig_find(SIG_LIST, n);
  if (!r)
    sig_missing(n);
  p = sz_signal_list_get((const SzSignalList *)r->sig);
  if (!sig_list_heads_str(p, r->elem_str))
    return sz_string_from_cstr("");
  i = 0;
  while (p) {
    if (i == index) {
      if (sig_head_str(p->head))
        return sz_string_from_cstr(
            sz_string_cstr((const SzString *)p->head));
      return sz_string_from_cstr("");
    }
    p = p->tail;
    i++;
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
  SigReg *r;
  if (!s)
    return;
  sig_unregister(s);
  /* A derived Signal.map borrows its source. Sever the link so a later
     get keeps the last computed value instead of reading freed memory. */
  for (r = g_sig_head; r; r = r->next) {
    if (r->kind == SIG_STR) {
      SzSignalStr *str = (SzSignalStr *)r->sig;
      if (str->map_src == s)
        str->map_src = NULL;
    }
  }
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
      return sig_list_heads_str(s ? s->value : NULL, r->elem_str);
  }
  return 0;
}

SzSignalList *sz_lang_signal_list(SzList *initial, SzString *name,
                                  int64_t elem_str) {
  SzSignalList *s = sz_signal_list(initial);
  sig_set_elem_str(s, elem_str);
  if (name)
    sz_signal_name(s, sz_string_cstr(name));
  return s;
}

/* Retain so last-use can drop. The C getter stays a borrow. */
SzList *sz_lang_signal_list_get(SzSignalList *s) {
  SzList *xs = sz_signal_list_get(s);
  sz_retain(xs);
  return xs;
}

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
