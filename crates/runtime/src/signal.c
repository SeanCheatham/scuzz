#define _POSIX_C_SOURCE 200809L

#include "scuzz_ui.h"

#include "rt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_signal_revision;
uint64_t sz_signal_revision(void) { return g_signal_revision; }

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
    sz_json_fputs_escaped(f, sz_string_cstr(value));
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

/* Value case tags from examples/compiler/src/Eval.scuzz. Keep this list
 * in source order. SCUZZ_EVAL_MIRROR rewrites a stored Value into the
 * compiled ADT shape. SCUZZ_EVAL_TAGS is `En.Case=tag` lines. */
static char *g_eval_tags;
static int g_eval_tags_ready;
static int g_eval_mirror;
static const char *g_eval_tags_path;

/* Read the mirror env once, before the UI worker starts.
 * A later getenv races with AppKit on the main thread. */
static void eval_env_init(void) __attribute__((constructor));
static void eval_env_init(void) {
  const char *arm = getenv("SCUZZ_EVAL_MIRROR");
  g_eval_mirror = arm && arm[0];
  g_eval_tags_path = getenv("SCUZZ_EVAL_TAGS");
}

static int eval_mirror_on(void) {
  return g_eval_mirror;
}

static void eval_tags_load(void) {
  const char *path;
  FILE *f;
  long n;
  if (g_eval_tags_ready)
    return;
  g_eval_tags_ready = 1;
  path = g_eval_tags_path;
  if (!path || !path[0])
    return;
  f = fopen(path, "rb");
  if (!f)
    return;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return;
  }
  n = ftell(f);
  if (n < 0)
    n = 0;
  if (n > 1024 * 1024)
    n = 1024 * 1024;
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return;
  }
  g_eval_tags = (char *)malloc((size_t)n + 1);
  if (!g_eval_tags) {
    fclose(f);
    return;
  }
  n = (long)fread(g_eval_tags, 1, (size_t)n, f);
  g_eval_tags[n] = 0;
  fclose(f);
}

/* 1 and writes *out when En.Case is in the tags file. Tag 0 is valid. */
static int eval_tag_of(const char *en, const char *name, int *out) {
  char key[300];
  const char *p;
  int n;
  eval_tags_load();
  if (!g_eval_tags || !out)
    return 0;
  n = snprintf(key, sizeof key, "%s.%s=", en ? en : "", name ? name : "");
  if (n <= 0 || n >= (int)sizeof key)
    return 0;
  p = g_eval_tags;
  while ((p = strstr(p, key)) != NULL) {
    if (p == g_eval_tags || p[-1] == '\n') {
      *out = atoi(p + n);
      return 1;
    }
    p += n;
  }
  return 0;
}

static void fputs_mirror_value(FILE *f, const void *value);

static void fputs_mirror_tuple(FILE *f, const SzList *xs) {
  if (!xs) {
    fputs("null", f);
    return;
  }
  if (!xs->tail) {
    fputs_mirror_value(f, xs->head);
    return;
  }
  fputc('[', f);
  fputs_mirror_value(f, xs->head);
  fputc(',', f);
  if (xs->tail->tail)
    fputs_mirror_tuple(f, xs->tail);
  else
    fputs_mirror_value(f, xs->tail->head);
  fputc(']', f);
}

static void fputs_mirror_fields(FILE *f, const SzList *fields) {
  const SzList *p;
  int n = 0;
  for (p = fields; p; p = p->tail)
    n++;
  if (n == 0) {
    fputs("null", f);
    return;
  }
  if (n == 1) {
    fputs_mirror_value(f, fields->head);
    return;
  }
  fputc('[', f);
  for (p = fields; p; p = p->tail) {
    if (p != fields)
      fputc(',', f);
    fputs_mirror_value(f, p->head);
  }
  fputc(']', f);
}

static void fputs_mirror_list(FILE *f, const void *payload) {
  const SzList *p =
      payload && sz_rc_kind(payload) == SZ_RC_LIST ? (const SzList *)payload : NULL;
  int first = 1;
  fputc('[', f);
  for (; p; p = p->tail) {
    if (!first)
      fputc(',', f);
    first = 0;
    fputs_mirror_value(f, p->head);
  }
  fputc(']', f);
}

static void fputs_mirror_con(FILE *f, const void *value, const void *pay) {
  const SzList *xs;
  const char *en = "";
  const char *name = "";
  const SzList *fields = NULL;
  int tag = 0;
  if (!pay || sz_rc_kind(pay) != SZ_RC_LIST) {
    fputs_json_value(f, value);
    return;
  }
  xs = (const SzList *)pay;
  if (!xs->tail || !xs->tail->tail) {
    fputs_json_value(f, value);
    return;
  }
  if (xs->head && sz_rc_kind(xs->head) == SZ_RC_STRING)
    en = sz_string_cstr((const SzString *)xs->head);
  if (xs->tail->head && sz_rc_kind(xs->tail->head) == SZ_RC_STRING)
    name = sz_string_cstr((const SzString *)xs->tail->head);
  if (xs->tail->tail->head && sz_rc_kind(xs->tail->tail->head) == SZ_RC_LIST)
    fields = (const SzList *)xs->tail->tail->head;
  if (!eval_tag_of(en, name, &tag)) {
    fputs_json_value(f, value);
    return;
  }
  fprintf(f, "{\"tag\":%d,\"payload\":", tag);
  fputs_mirror_fields(f, fields);
  fputc('}', f);
}

/* Rewrite interpreter Value ADTs into the compiled dump shape. */
static void fputs_mirror_value(FILE *f, const void *value) {
  int tag;
  const void *pay;
  if (!eval_mirror_on() || !value || sz_rc_kind(value) != SZ_RC_ADT) {
    fputs_json_value(f, value);
    return;
  }
  tag = sz_adt_tag((const SzAdt *)value);
  pay = sz_adt_payload((const SzAdt *)value);
  if (tag == 0) {
    fputs("null", f);
    return;
  }
  if (tag == 1 || tag == 2 || tag == 3) {
    fputs_json_value(f, pay);
    return;
  }
  if (tag == 4) {
    fputs_mirror_list(f, pay);
    return;
  }
  if (tag == 5) {
    fputs_mirror_tuple(f, pay && sz_rc_kind(pay) == SZ_RC_LIST ? (const SzList *)pay : NULL);
    return;
  }
  if (tag == 6) {
    fputs_mirror_con(f, value, pay);
    return;
  }
  fputs_json_value(f, value);
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
    sz_json_fputs_escaped(f, r->name);
    fputs("\",\"value\":", f);
    switch (r->kind) {
    case SIG_INT:
      fprintf(f, "%lld",
              (long long)sz_signal_int_get((const SzSignalInt *)r->sig));
      break;
    case SIG_STR:
      fputc('"', f);
      sz_json_fputs_escaped(f, sz_signal_str_get((const SzSignalStr *)r->sig));
      fputc('"', f);
      break;
    case SIG_VALUE: {
      void *value = sz_signal_read((SzSignal *)r->sig);
      fputs_mirror_value(f, value);
      sz_release(value);
      break;
    }
    case SIG_LIST: {
      const SzSignalList *ls = (const SzSignalList *)r->sig;
      SzList *p = sz_signal_list_get(ls);
      int first = 1;
      if (!sig_list_heads_str(p, ls->elem_str)) {
        if (!p)
          fputs("[]", f);
        else
          fputs_json_value(f, p);
        break;
      }
      fputc('[', f);
      for (; p; p = p->tail) {
        if (!first)
          fputc(',', f);
        first = 0;
        fputc('"', f);
        sz_json_fputs_escaped(f, sig_head_str(p->head)
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

SzString *sz_signal_dump_json_string(void) {
  char *buf = NULL;
  size_t len = 0;
  FILE *f = open_memstream(&buf, &len);
  SzString *out;
  if (!f)
    return sz_string_from_cstr("[]");
  sz_signal_dump_json(f);
  fclose(f);
  out = sz_string_from_cstr(buf ? buf : "[]");
  free(buf);
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

/* Drop the current value. A list releases its heads. A view head dies when
 * this signal alone held that list. */
static void drop_value(SzSignal *s, void *value) {
  (void)s;
  sz_release(value);
}

static void *signal_value(SzSignal *s) {
  void *out;
  if (!s) return NULL;
  if (!s->map_src) return s->value;
  (void)signal_value(s->map_src);
  if (s->map_valid && s->map_seen == s->map_src->version) return s->value;
  out = s->map_fn(s->map_src->value, s->map_env);
  drop_value(s, s->value);
  s->value = out;
  s->map_seen = s->map_src->version;
  s->map_valid = 1;
  s->version++;
  return s->value;
}

/* Evaluator mirrors keep Signal.get working and stay out of the dump.
 * SCUZZ_EVAL_MIRROR arms this. The name is $mirror. */
static int sig_is_mirror(SzString *name) {
  const char *n;
  if (!g_eval_mirror || !name)
    return 0;
  n = sz_string_cstr(name);
  return n && strcmp(n, "$mirror") == 0;
}

SzSignal *sz_signal_new(void *value, int64_t kind, SzString *name) {
  SzSignal *s = sz_alloc_zero(sizeof(*s));
  sz_retain(value);
  s->value = value;
  s->elem_str = kind == 3;
  if (sig_is_mirror(name))
    return s;
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
  drop_value(s, s->value);
  s->value = value;
  s->version++;
  g_signal_revision++;
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
  drop_value(s, s->value);
  sz_release(s->map_env);
  sz_free(s);
}

/* A signal is not reference counted. Ui.run keeps the signals that existed
 * at session start and drops values the script wrote. Nested runs stack. */
typedef struct SigHold {
  SzSignal *sig;
  void *value;
  struct SigHold *next;
} SigHold;

typedef struct SigFrame {
  int mark;
  int armed;
  SigHold *holds;
  struct SigFrame *prev;
} SigFrame;

static SigFrame *g_sig_frame = NULL;

static int sig_registered(const SzSignal *s) {
  SigReg *r;
  for (r = g_sig_head; r; r = r->next)
    if (r->sig == s)
      return 1;
  return 0;
}

static void free_signals_from(int mark) {
  for (;;) {
    SigReg *r;
    SigReg *best = NULL;
    for (r = g_sig_head; r; r = r->next)
      if (r->id >= mark && (!best || r->id > best->id))
        best = r;
    if (!best)
      return;
    sz_signal_free((SzSignal *)best->sig);
  }
}

static void restore_holds(SigHold *h) {
  while (h) {
    SigHold *next = h->next;
    if (sig_registered(h->sig) && h->sig->value != h->value) {
      void *cur = h->sig->value;
      h->sig->value = h->value;
      h->sig->map_valid = 0;
      h->value = NULL;
      sz_release(cur);
    }
    sz_release(h->value);
    sz_free(h);
    h = next;
  }
}

void sz_signal_session_push(void) {
  SigFrame *f = (SigFrame *)sz_alloc_zero(sizeof(*f));
  SigReg *r;
  f->mark = g_sig_next_id;
  f->armed = sz_testrt_oracles_armed();
  f->prev = g_sig_frame;
  if (f->armed) {
    for (r = g_sig_head; r; r = r->next) {
      SigHold *h = (SigHold *)sz_alloc_zero(sizeof(*h));
      h->sig = (SzSignal *)r->sig;
      h->value = sz_signal_read(h->sig);
      h->next = f->holds;
      f->holds = h;
    }
  }
  g_sig_frame = f;
}

void sz_signal_session_pop(void) {
  SigFrame *f = g_sig_frame;
  if (!f)
    return;
  g_sig_frame = f->prev;
  if (f->armed) {
    free_signals_from(f->mark);
    restore_holds(f->holds);
  }
  sz_free(f);
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
SzString *sz_signal_show(const void *sig) {
  (void)sig;
  return sz_string_from_cstr("<signal>");
}
void sz_signal_list_free(SzSignalList *s) { sz_signal_free(s); }
int sz_signal_list_elem_str(const SzSignalList *s) {
  return s ? sig_list_heads_str(signal_value((SzSignal *)s), s->elem_str) : 0;
}
