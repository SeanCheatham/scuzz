#define _POSIX_C_SOURCE 200809L

#include "scuzz_ui.h"

#include "rt_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_signal_revision;

/* A tap and a request callback run on different threads on Apple ARM.
 * Publish the value there. Other hosts keep a plain load and store.
 * The evaluator reads signals on the type-check path. An atomic on that
 * path makes the idle probe miss its deadline. */
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define SIGNAL_PUBLISH 1
#else
#define SIGNAL_PUBLISH 0
#endif

uint64_t sz_signal_revision(void) {
#if SIGNAL_PUBLISH
  return __atomic_load_n(&g_signal_revision, __ATOMIC_ACQUIRE);
#else
  return g_signal_revision;
#endif
}

typedef struct SigReg SigReg;

struct SzSignalScope { SigReg *head; };
static _Thread_local SzSignalScope *g_signal_scope;

struct SzSignal {
  void *value;
  int elem_str;
  int value_cell;
  int hidden;
  uint64_t version;
  SigReg *registry;
  SzSignal *map_src;
  SzSignal *map_children, *map_last_child, *map_prev, *map_next;
  SzSignalMapFn map_fn;
  void *map_env;
  uint64_t map_seen;
  int map_valid;
};

static void *signal_load(const SzSignal *s) {
#if SIGNAL_PUBLISH
  return __atomic_load_n(&s->value, __ATOMIC_ACQUIRE);
#else
  return s->value;
#endif
}

static void signal_store(SzSignal *s, void *value) {
#if SIGNAL_PUBLISH
  __atomic_store_n(&s->value, value, __ATOMIC_RELEASE);
#else
  s->value = value;
#endif
}

static uint64_t signal_version(const SzSignal *s) {
#if SIGNAL_PUBLISH
  return __atomic_load_n(&s->version, __ATOMIC_ACQUIRE);
#else
  return s->version;
#endif
}

static void signal_bump_version(SzSignal *s) {
#if SIGNAL_PUBLISH
  __atomic_fetch_add(&s->version, 1, __ATOMIC_RELEASE);
#else
  s->version++;
#endif
}

static void signal_bump_revision(void) {
#if SIGNAL_PUBLISH
  __atomic_fetch_add(&g_signal_revision, 1, __ATOMIC_RELEASE);
#else
  g_signal_revision++;
#endif
}

/* --- signal store registry (fuzz / dump oracle) --------------------------- */
/* Register every live signal for session cleanup. Hidden signals have no
 * dump ID. Visible IDs increase in creation order and stay stable after free. */

typedef enum { SIG_INT = 1, SIG_STR = 2, SIG_LIST = 3, SIG_VALUE = 4 } SigKind;

struct SigReg {
  SigKind kind;
  int id;
  uint64_t serial;
  char *name;
  const void *sig;
  SigReg *next, *prev;
  SzSignalScope *scope;
  SigReg *scope_next, *scope_prev;
};

static SzMap *g_signal_names;
static SigReg *g_sig_head = NULL;
static SigReg *g_sig_tail = NULL;
static int g_sig_next_id = 0;
static uint64_t g_sig_next_serial = 0;

static void sig_register(SigKind kind, const void *sig, int hidden) {
  SigReg *r = (SigReg *)sz_alloc_zero(sizeof(SigReg));
  r->kind = kind;
  r->id = hidden ? -1 : g_sig_next_id++;
  r->serial = g_sig_next_serial++;
  r->name = sz_strdup("");
  r->sig = sig;
  ((SzSignal *)sig)->registry = r;
  r->prev = g_sig_tail;
  r->scope = g_signal_scope;
  if (r->scope) {
    r->scope_next = r->scope->head;
    if (r->scope_next) r->scope_next->scope_prev = r;
    r->scope->head = r;
  }
  if (g_sig_tail)
    g_sig_tail->next = r;
  else
    g_sig_head = r;
  g_sig_tail = r;
}

static SzPair *signal_name_key(SigKind kind, const char *name) {
  void *tag = sz_box_i64(kind);
  SzString *text = sz_string_from_cstr(name);
  SzPair *key = sz_pair_new(tag, text);
  sz_release(tag);
  sz_release(text);
  return key;
}

static void signal_remove_name(SigReg *r) {
  if (!r->name[0]) return;
  SzPair *key = signal_name_key(r->kind, r->name);
  SzMap *next = sz_map_remove(g_signal_names, key);
  sz_release(g_signal_names);
  g_signal_names = next;
  sz_release(key);
}

/* The index borrows registry entries. Remove each entry before release. */
static void signal_set_name(SigReg *mine, const char *name) {
  const char *n = name ? name : "";
  if (mine->id < 0 || !strcmp(mine->name, n)) return;
  SzPair *key = signal_name_key(mine->kind, n);
  signal_remove_name(mine);
  if (n[0]) {
    SigReg *other = sz_map_get_or(g_signal_names, key, NULL);
    if (other && other != mine) {
      sz_free(other->name);
      other->name = sz_strdup("");
    }
    SzMap *next = sz_map_set(g_signal_names, key, mine, 1);
    sz_release(g_signal_names);
    g_signal_names = next;
  }
  sz_free(mine->name);
  mine->name = sz_strdup(n);
  sz_release(key);
}

void sz_signal_name(const void *sig, const char *name) {
  for (SigReg *r = g_sig_head; r; r = r->next) {
    if (r->sig == sig) {
      signal_set_name(r, name);
      return;
    }
  }
}

static void sig_unregister(const void *sig) {
  SigReg *r = ((const SzSignal *)sig)->registry;
  signal_remove_name(r);
  if (r->prev) r->prev->next = r->next;
  else g_sig_head = r->next;
  if (r->next) r->next->prev = r->prev;
  else g_sig_tail = r->prev;
  if (r->scope) {
    if (r->scope_prev) r->scope_prev->scope_next = r->scope_next;
    else r->scope->head = r->scope_next;
    if (r->scope_next) r->scope_next->scope_prev = r->scope_prev;
  }
  sz_free(r->name);
  sz_free(r);
}

static SigReg *sig_find(SigKind kind, const char *name) {
  if (!name || !name[0]) return NULL;
  SzPair *key = signal_name_key(kind, name);
  SigReg *r = sz_map_get_or(g_signal_names, key, NULL);
  sz_release(key);
  return r;
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
/* The runtime uses -1 when the evaluator is absent.
 * The compiler sets the tag of Eval.Value.VUnit when it is present.
 * The compiler also sets the exact VView tag.
 * Scalar and container Value tags follow the source order. */
__attribute__((weak)) const int32_t sz_eval_value_tag_base = -1;
__attribute__((weak)) const int32_t sz_eval_view_tag = -1;

static int eval_value_base(void) {
  return sz_eval_value_tag_base;
}

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

/* 1 and writes *out when En.Case is a built-in or in the tags file. Tag 0 is valid. */
static int eval_tag_of(const char *en, const char *name, int *out) {
  char key[300];
  const char *p;
  int n;
  if (!out)
    return 0;
  if (en && name) {
    if (strcmp(en, "Option") == 0) {
      if (strcmp(name, "None") == 0) {
        *out = 0;
        return 1;
      }
      if (strcmp(name, "Some") == 0) {
        *out = 1;
        return 1;
      }
    } else if (strcmp(en, "Result") == 0) {
      if (strcmp(name, "Err") == 0) {
        *out = 0;
        return 1;
      }
      if (strcmp(name, "Ok") == 0) {
        *out = 1;
        return 1;
      }
    } else if (strcmp(en, "Json") == 0) {
      static const char *const json_cases[] = {
          "Null", "Bool", "Int", "Float", "Str", "Arr", "Obj"};
      for (int i = 0; i < 7; i++) {
        if (strcmp(name, json_cases[i]) == 0) {
          *out = i;
          return 1;
        }
      }
    }
  }
  eval_tags_load();
  if (!g_eval_tags)
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
    fputs("\"<handle>\"", f);
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
  if (eval_value_base() < 0) {
    fputs_json_value(f, value);
    return;
  }
  if (sz_adt_tag((const SzAdt *)value) == sz_eval_view_tag) {
    fputs("\"<handle>\"", f);
    return;
  }
  tag = sz_adt_tag((const SzAdt *)value) - eval_value_base();
  pay = sz_adt_payload((const SzAdt *)value);
  if (tag == 0) {
    fputs("null", f);
    return;
  }
  if (tag == 1 || tag == 2 || tag == 3 || tag == 15) {
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
  if (tag >= 7) {
    fputs("\"<handle>\"", f);
    return;
  }
  fputs_json_value(f, value);
}

/* A mirrored Value list is a list in the dump. Claims read that type. */
static int mirror_list_value(const void *value) {
  return eval_mirror_on() && value && sz_rc_kind(value) == SZ_RC_ADT &&
         eval_value_base() >= 0 &&
         sz_adt_tag((const SzAdt *)value) == eval_value_base() + 4;
}

/* Typed session schema: one object per registered signal. Int payloads
 * are numbers. Str payloads are strings. Value and list payloads encode
 * typed (schema v=2). */
void sz_signal_dump_json(FILE *f) {
  SigReg *r;
  int first = 1;
  fputc('[', f);
  for (r = g_sig_head; r; r = r->next) {
    const SzSignal *sig = (const SzSignal *)r->sig;
    void *held = NULL;
    int value_list = 0;
    if (r->id < 0)
      continue;
    if (!first)
      fputc(',', f);
    first = 0;
    if (sig->value_cell) {
      held = sz_signal_read((SzSignal *)sig);
      value_list = mirror_list_value(held);
    }
    fprintf(f, "{\"id\":%d,\"type\":\"", r->id);
    fputs(r->kind == SIG_INT ? "int" : r->kind == SIG_STR ? "str"
                                     : r->kind == SIG_LIST || value_list ? "list"
                                                                         : "value",
          f);
    fputs("\",\"name\":\"", f);
    sz_json_fputs_escaped(f, r->name);
    fputs("\",\"value\":", f);
    if (sig->value_cell) {
      fputs_mirror_value(f, held);
      sz_release(held);
    } else {
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
        void *fallback = sz_signal_read((SzSignal *)r->sig);
        fputs_mirror_value(f, fallback);
        sz_release(fallback);
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

static void *signal_value(SzSignal *s);

static const SzList *sig_list_items(const SzSignal *s) {
  void *v = signal_value((SzSignal *)s);
  if (!v)
    return NULL;
  if (s->value_cell) {
    const void *pay =
        mirror_list_value(v) ? sz_adt_payload((const SzAdt *)v) : NULL;
    return pay && sz_rc_kind(pay) == SZ_RC_LIST ? (const SzList *)pay : NULL;
  }
  return sz_rc_kind(v) == SZ_RC_LIST ? (const SzList *)v : NULL;
}

static const char *mirror_head_cstr(const void *head) {
  const void *pay;
  if (!eval_mirror_on() || !head || sz_rc_kind(head) != SZ_RC_ADT ||
      eval_value_base() < 0)
    return NULL;
  if (sz_adt_tag((const SzAdt *)head) != eval_value_base() + 3)
    return NULL;
  pay = sz_adt_payload((const SzAdt *)head);
  return pay && sz_rc_kind(pay) == SZ_RC_STRING
             ? sz_string_cstr((const SzString *)pay)
             : NULL;
}

int64_t sz_property_signal_list_len(SzString *name) {
  const char *n = name ? sz_string_cstr(name) : "";
  SigReg *r;
  if (sz_timeline_replaying())
    return sz_timeline_replay_signal_list_len(n);
  r = sig_find(SIG_LIST, n);
  if (!r)
    sig_missing(n);
  return (int64_t)sz_list_len(sig_list_items((const SzSignal *)r->sig));
}

SzString *sz_property_signal_list_at(SzString *name, int64_t index) {
  SigReg *r;
  const SzSignal *sig;
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
  sig = (const SzSignal *)r->sig;
  p = sig_list_items(sig);
  if (sig->value_cell) {
    for (const SzList *q = p; q; q = q->tail) {
      if (!mirror_head_cstr(q->head))
        return sz_string_from_cstr("");
    }
    i = 0;
    while (p) {
      if (i == index) {
        const char *text = mirror_head_cstr(p->head);
        return sz_string_from_cstr(text ? text : "");
      }
      p = p->tail;
      i++;
    }
    return sz_string_from_cstr("");
  }
  if (!sig_list_heads_str(p, sig->elem_str))
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
  if (!s->map_src) return signal_load(s);
  (void)signal_value(s->map_src);
  if (s->map_valid && s->map_seen == signal_version(s->map_src))
    return signal_load(s);
  SzSignalScope *previous = sz_signal_scope_enter(s->registry->scope);
  out = s->map_fn(signal_load(s->map_src), s->map_env);
  sz_signal_scope_enter(previous);
  drop_value(s, signal_load(s));
  signal_store(s, out);
  s->map_seen = signal_version(s->map_src);
  s->map_valid = 1;
  signal_bump_version(s);
  return signal_load(s);
}

/* Internal mirrors keep Signal.get working and stay out of every dump.
 * The name is $mirror. */
static int sig_is_mirror(SzString *name) {
  const char *n;
  if (!name)
    return 0;
  n = sz_string_cstr(name);
  return n && strcmp(n, "$mirror") == 0;
}

SzSignal *sz_signal_new(void *value, int64_t kind, SzString *name) {
  SigKind reg_kind = (SigKind)(kind == 5 ? SIG_LIST
                               : (kind == 4 && mirror_list_value(value))
                                   ? SIG_LIST
                                   : kind);
  SzSignal *s = sz_alloc_zero(sizeof(*s));
  sz_retain(value);
  signal_store(s, value);
  s->elem_str = kind == 3;
  s->value_cell = kind == 4;
  s->hidden = sig_is_mirror(name);
  sig_register(reg_kind, s, s->hidden);
  if (name) signal_set_name(s->registry, sz_string_cstr(name));
  return s;
}

void *sz_signal_read(SzSignal *s) {
  void *value = signal_value(s);
  sz_retain(value);
  return value;
}

void *sz_signal_write(SzSignal *s, void *value) {
  void *cur;
  if (!s || s->map_fn) return NULL;
  cur = signal_load(s);
  if (sz_ptr_eq(cur, value)) return NULL;
  sz_retain(value);
  drop_value(s, cur);
  signal_store(s, value);
  if (s->value_cell && !s->hidden && mirror_list_value(value) &&
      s->registry && s->registry->kind != SIG_LIST) {
    char *saved = s->registry->name[0] ? sz_strdup(s->registry->name) : NULL;
    signal_remove_name(s->registry);
    sz_free(s->registry->name);
    s->registry->name = sz_strdup("");
    s->registry->kind = SIG_LIST;
    if (saved) {
      signal_set_name(s->registry, saved);
      sz_free(saved);
    }
  }
  signal_bump_version(s);
  if (!s->hidden)
    signal_bump_revision();
  return NULL;
}

SzSignal *sz_signal_derive(SzSignal *src, SzSignalMapFn fn, void *env,
                           int64_t kind, SzString *name) {
  SzSignal *s = sz_signal_new(NULL, kind, sig_is_mirror(name) ? name : NULL);
  s->map_src = src;
  if (src) {
    s->map_prev = src->map_last_child;
    if (s->map_prev) s->map_prev->map_next = s;
    else src->map_children = s;
    src->map_last_child = s;
  }
  s->map_fn = fn;
  sz_retain(env);
  s->map_env = env;
  (void)signal_value(s);
  if (s->value_cell && mirror_list_value(signal_load(s)))
    s->registry->kind = SIG_LIST;
  if (name && !s->hidden)
    signal_set_name(s->registry, sz_string_cstr(name));
  return s;
}

static void signal_unlink_source(SzSignal *s) {
  if (!s->map_src) return;
  if (s->map_prev) s->map_prev->map_next = s->map_next;
  else s->map_src->map_children = s->map_next;
  if (s->map_next) s->map_next->map_prev = s->map_prev;
  else s->map_src->map_last_child = s->map_prev;
  s->map_src = NULL;
  s->map_prev = s->map_next = NULL;
}

void sz_signal_free(SzSignal *s) {
  if (!s) return;
  while (s->map_children) {
    SzSignal *child = s->map_children;
    (void)signal_value(child);
    signal_unlink_source(child);
  }
  signal_unlink_source(s);
  sig_unregister(s);
  drop_value(s, signal_load(s));
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
  uint64_t mark;
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

SzSignalScope *sz_signal_scope_new(void) {
  return sz_alloc_zero(sizeof(SzSignalScope));
}

SzSignalScope *sz_signal_scope_enter(SzSignalScope *scope) {
  SzSignalScope *previous = g_signal_scope;
  g_signal_scope = scope;
  return previous;
}

void sz_signal_scope_free(SzSignalScope *scope) {
  if (!scope) return;
  if (g_signal_scope == scope) sz_panic("Signal scope is active during release");
  while (scope->head) sz_signal_free((SzSignal *)scope->head->sig);
  sz_free(scope);
}

static void free_signals_from(uint64_t mark) {
  while (g_sig_tail && g_sig_tail->serial >= mark)
    sz_signal_free((SzSignal *)g_sig_tail->sig);
}

static void restore_holds(SigHold *h) {
  while (h) {
    SigHold *next = h->next;
    if (sig_registered(h->sig) && signal_load(h->sig) != h->value) {
      void *cur = signal_load(h->sig);
      signal_store(h->sig, h->value);
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
  f->mark = g_sig_next_serial;
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
