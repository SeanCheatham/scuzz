#include "scuzz_ui.h"

#include "rt_util.h"

#include <stdio.h>
#include <string.h>

struct SzSignal {
  void *value;
  int elem_str;
  uint64_t version;
  SzSignal *map_src;
  SzSignalMapFn map_fn;
  void *map_env;
  uint64_t map_seen;
  int map_valid;
};

/* --- signal store registry (fuzz / dump oracle) --------------------------- */
/* Every live signal, in creation order. Ids are monotonic. A dump stays
   stable across frees. */

typedef enum { SIG_INT = 1, SIG_STR = 2, SIG_LIST = 3, SIG_VALUE = 4 } SigKind;

typedef struct SigReg {
  SigKind kind;
  int id;
  char *name;
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
  if (strcmp(mine->name, n) == 0)
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
  SigReg *prev = NULL;
  while (*p) {
    if ((*p)->sig == sig) {
      SigReg *dead = *p;
      *p = dead->next;
      if (g_sig_tail == dead)
        g_sig_tail = prev;
      sz_free(dead->name);
      sz_free(dead);
      return;
    }
    prev = *p;
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
  char *buf = NULL;
  size_t len = 0, cap = 0;
  sz_dump_append(&buf, &len, &cap, "missing signal ");
  sz_dump_append(&buf, &len, &cap, name && name[0] ? name : "(empty)");
  sz_panic(buf); /* noreturn: buf dies with the process */
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

/* 1 when the head is a String the dump may print, 0 otherwise. */
static int sig_head_str(const void *head) {
  return head && sz_rc_kind(head) == SZ_RC_STRING;
}

static void sig_dump_value(char **buf, size_t *len, size_t *cap, const void *value) {
  char number[64];
  uint32_t kind = sz_rc_kind(value);
  if (!value) { sz_dump_append(buf, len, cap, "()"); return; }
  if (kind == SZ_RC_STRING) {
    sz_dump_append(buf, len, cap, "\"");
    sz_dump_append_escaped(buf, len, cap, sz_string_cstr(value));
    sz_dump_append(buf, len, cap, "\"");
  } else if (kind == SZ_RC_BOX) {
    snprintf(number, sizeof number, "%lld", (long long)sz_unbox_i64(value));
    sz_dump_append(buf, len, cap, number);
  } else if (kind == SZ_RC_ADT) {
    snprintf(number, sizeof number, "%d(", sz_adt_tag(value));
    sz_dump_append(buf, len, cap, number);
    sig_dump_value(buf, len, cap, sz_adt_payload(value));
    sz_dump_append(buf, len, cap, ")");
  } else if (kind == SZ_RC_LIST) {
    const SzList *p = value;
    sz_dump_append(buf, len, cap, "[");
    for (; p; p = p->tail) {
      sig_dump_value(buf, len, cap, p->head);
      if (p->tail) sz_dump_append(buf, len, cap, ",");
    }
    sz_dump_append(buf, len, cap, "]");
  } else {
    sz_dump_append(buf, len, cap, "<handle>");
  }
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
                                                                    : r->kind == SIG_LIST ? "list" : "value");
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
    case SIG_VALUE: {
      void *value = sz_signal_read((SzSignal *)r->sig);
      sig_dump_value(&buf, &len, &cap, value);
      sz_dump_append(&buf, &len, &cap, "\n");
      sz_release(value);
      break;
    }
    case SIG_LIST: {
      const SzSignalList *ls = (const SzSignalList *)r->sig;
      SzList *p = sz_signal_list_get(ls);
      if (!sig_list_heads_str(p, ls->elem_str)) {
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

static void fputs_json_escaped(FILE *f, const char *s) {
  const char *p;
  if (!s)
    return;
  for (p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '\\')
      fputs("\\\\", f);
    else if (c == '"')
      fputs("\\\"", f);
    else if (c == '\n')
      fputs("\\n", f);
    else if (c == '\r')
      fputs("\\r", f);
    else if (c == '\t')
      fputs("\\t", f);
    else if (c < 0x20)
      fprintf(f, "\\u%04x", c);
    else
      fputc(*p, f);
  }
}

/* Typed session schema v=2: a value payload as JSON. A String is a string.
 * A boxed Int is a number. An ADT is {"tag":N,"payload":...}. A pair is a
 * two-slot array. A List is an array. A handle stays "<handle>". */
static void fputs_json_value(FILE *f, const void *value) {
  uint32_t kind;
  if (!value) {
    fputs("null", f);
    return;
  }
  kind = sz_rc_kind(value);
  if (kind == SZ_RC_STRING) {
    fputc('"', f);
    fputs_json_escaped(f, sz_string_cstr(value));
    fputc('"', f);
  } else if (kind == SZ_RC_BOX) {
    fprintf(f, "%lld", (long long)sz_unbox_i64(value));
  } else if (kind == SZ_RC_ADT) {
    fprintf(f, "{\"tag\":%d,\"payload\":", sz_adt_tag(value));
    fputs_json_value(f, sz_adt_payload(value));
    fputc('}', f);
  } else if (kind == SZ_RC_PAIR) {
    fputc('[', f);
    fputs_json_value(f, sz_pair_left((const SzPair *)value));
    fputc(',', f);
    fputs_json_value(f, sz_pair_right((const SzPair *)value));
    fputc(']', f);
  } else if (kind == SZ_RC_LIST) {
    const SzList *p = value;
    int first = 1;
    fputc('[', f);
    for (; p; p = p->tail) {
      if (!first)
        fputc(',', f);
      first = 0;
      fputs_json_value(f, p->head);
    }
    fputc(']', f);
  } else {
    fputs("\"<handle>\"", f);
  }
}

/* Typed session schema: one object per registered signal. Int payloads
 * are numbers. Str payloads are strings. Value and list payloads encode
 * typed (schema v=2). */
void sz_signal_dump_json(FILE *f) {
  SigReg *r;
  int first = 1;
  fputc('[', f);
  for (r = g_sig_head; r; r = r->next) {
    if (!first)
      fputc(',', f);
    first = 0;
    fprintf(f, "{\"id\":%d,\"type\":\"", r->id);
    fputs(r->kind == SIG_INT ? "int" : r->kind == SIG_STR ? "str"
                                     : r->kind == SIG_LIST ? "list" : "value",
          f);
    fputs("\",\"name\":\"", f);
    fputs_json_escaped(f, r->name);
    fputs("\",\"value\":", f);
    switch (r->kind) {
    case SIG_INT:
      fprintf(f, "%lld",
              (long long)sz_signal_int_get((const SzSignalInt *)r->sig));
      break;
    case SIG_STR:
      fputc('"', f);
      fputs_json_escaped(f, sz_signal_str_get((const SzSignalStr *)r->sig));
      fputc('"', f);
      break;
    case SIG_VALUE: {
      void *value = sz_signal_read((SzSignal *)r->sig);
      fputs_json_value(f, value);
      sz_release(value);
      break;
    }
    case SIG_LIST: {
      const SzSignalList *ls = (const SzSignalList *)r->sig;
      SzList *p = sz_signal_list_get(ls);
      int first = 1;
      if (!sig_list_heads_str(p, ls->elem_str)) {
        fputs_json_value(f, p);
        break;
      }
      fputc('[', f);
      for (; p; p = p->tail) {
        if (!first)
          fputc(',', f);
        first = 0;
        fputc('"', f);
        fputs_json_escaped(f, sig_head_str(p->head)
                                  ? sz_string_cstr((const SzString *)p->head)
                                  : "");
        fputc('"', f);
      }
      fputc(']', f);
      break;
    }
    }
    fputc('}', f);
  }
  fputc(']', f);
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
  if (!sig_list_heads_str(p, ((const SzSignalList *)r->sig)->elem_str))
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

static void *signal_value(SzSignal *s) {
  void *out;
  if (!s) return NULL;
  if (!s->map_src) return s->value;
  (void)signal_value(s->map_src);
  if (s->map_valid && s->map_seen == s->map_src->version) return s->value;
  out = s->map_fn(s->map_src->value, s->map_env);
  sz_release(s->value);
  s->value = out;
  s->map_seen = s->map_src->version;
  s->map_valid = 1;
  s->version++;
  return s->value;
}

SzSignal *sz_signal_new(void *value, int64_t kind, SzString *name) {
  SzSignal *s = sz_alloc_zero(sizeof(*s));
  sz_retain(value);
  s->value = value;
  s->elem_str = kind == 3;
  sig_register((SigKind)(kind == 5 ? 3 : kind), s);
  if (name) sz_signal_name(s, sz_string_cstr(name));
  return s;
}

void *sz_signal_read(SzSignal *s) {
  void *value = signal_value(s);
  sz_retain(value);
  return value;
}

void *sz_signal_write(SzSignal *s, void *value) {
  if (!s || s->map_fn) return NULL;
  if (s->value == value || sz_ptr_eq(s->value, value)) return NULL;
  sz_retain(value);
  sz_release(s->value);
  s->value = value;
  s->version++;
  return NULL;
}

SzSignal *sz_signal_derive(SzSignal *src, SzSignalMapFn fn, void *env,
                           int64_t kind, SzString *name) {
  SzSignal *s = sz_signal_new(NULL, kind, name);
  s->map_src = src;
  s->map_fn = fn;
  sz_retain(env);
  s->map_env = env;
  (void)signal_value(s);
  return s;
}

void sz_signal_free(SzSignal *s) {
  SigReg *r;
  if (!s) return;
  for (r = g_sig_head; r; r = r->next) {
    SzSignal *child = (SzSignal *)r->sig;
    if (child->map_src == s) {
      (void)signal_value(child);
      child->map_src = NULL;
    }
  }
  sig_unregister(s);
  sz_release(s->value);
  sz_release(s->map_env);
  sz_free(s);
}

SzSignalInt *sz_signal_int(int64_t initial) {
  void *box = sz_box_i64(initial);
  SzSignal *s = sz_signal_new(box, 1, NULL);
  sz_release(box);
  return s;
}
void sz_signal_int_set(SzSignalInt *s, int64_t value) {
  void *box = sz_box_i64(value);
  sz_signal_write(s, box);
  sz_release(box);
}
int64_t sz_signal_int_get(const SzSignalInt *s) {
  return s ? sz_unbox_i64(signal_value((SzSignal *)s)) : 0;
}
void sz_signal_int_free(SzSignalInt *s) { sz_signal_free(s); }
SzSignalStr *sz_signal_str(const char *initial) {
  SzString *value = sz_string_from_cstr(initial ? initial : "");
  SzSignal *s = sz_signal_new(value, 2, NULL);
  sz_release(value);
  return s;
}
void sz_signal_str_set(SzSignalStr *s, const char *value) {
  SzString *str = sz_string_from_cstr(value ? value : "");
  sz_signal_write(s, str);
  sz_release(str);
}
const char *sz_signal_str_get(const SzSignalStr *s) {
  SzString *value = signal_value((SzSignal *)s);
  return value ? sz_string_cstr(value) : "";
}
void sz_signal_str_free(SzSignalStr *s) { sz_signal_free(s); }
SzSignalList *sz_signal_list(SzList *initial) { return sz_signal_new(initial, 3, NULL); }
void sz_signal_list_set(SzSignalList *s, SzList *value) { sz_signal_write(s, value); }
SzList *sz_signal_list_get(const SzSignalList *s) { return signal_value((SzSignal *)s); }
void sz_signal_list_free(SzSignalList *s) { sz_signal_free(s); }
int sz_signal_list_elem_str(const SzSignalList *s) {
  return s ? sig_list_heads_str(signal_value((SzSignal *)s), s->elem_str) : 0;
}
