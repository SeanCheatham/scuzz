/* Android sz_mobile_* shell. Strong defs override the weak runtime stubs.
 * Clipboard stays in-process (g_clip). The OS clipboard needs a main-looper
 * hop this shell does not take. */

#define _POSIX_C_SOURCE 200809L

#include "scuzz_mobile.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 64

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_alive;
static int g_motion_value, g_motion_reported;
static int g_frames;
static int g_w;
static int g_h;
static _Atomic uint32_t g_keyboard_request;
static uint8_t *g_rgba;
static size_t g_rgba_cap;
static char *g_text_bufs[TEXT_RING];
static int g_text_i;
static char *g_poll_text;
static char *g_clip;

static pthread_mutex_t g_a11y_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_a11y_json;
static uint64_t g_a11y_version;

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return atomic_load(&g_alive); }

int sz_mobile_reduced_motion(void) {
  pthread_mutex_lock(&g_q_lock);
  int reduced = g_motion_value;
  g_motion_reported = reduced;
  pthread_mutex_unlock(&g_q_lock);
  return reduced;
}

void scuzz_android_set_reduced_motion(int reduced) {
  pthread_mutex_lock(&g_q_lock);
  g_motion_value = reduced != 0;
  pthread_mutex_unlock(&g_q_lock);
}

void scuzz_android_set_alive(int alive) { atomic_store(&g_alive, alive ? 1 : 0); }

void sz_mobile_shutdown(void) {
  int i;
  pthread_mutex_lock(&g_frame_lock);
  free(g_rgba);
  g_rgba = NULL;
  g_rgba_cap = 0;
  g_w = g_h = 0;
  pthread_mutex_unlock(&g_frame_lock);
  pthread_mutex_lock(&g_q_lock);
  for (i = 0; i < TEXT_RING; i++) {
    free(g_text_bufs[i]);
    g_text_bufs[i] = NULL;
  }
  free(g_poll_text);
  g_poll_text = NULL;
  g_motion_reported = g_motion_value;
  g_q_head = g_q_tail = g_text_i = 0;
  pthread_mutex_unlock(&g_q_lock);
  pthread_mutex_lock(&g_a11y_lock);
  free(g_a11y_json);
  g_a11y_json = NULL;
  g_a11y_version++;
  pthread_mutex_unlock(&g_a11y_lock);
  free(g_clip);
  g_clip = NULL;
  scuzz_android_set_alive(0);
}

static int q_full(void) { return ((g_q_tail + 1) % EVENT_CAP) == g_q_head; }

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    if ((g_queue[i].kind == SZ_INPUT_TEXT_EDIT ||
         g_queue[i].kind == SZ_INPUT_ACCESSIBILITY) && g_queue[i].text == slot)
      return 1;
  }
  return 0;
}

static const char *stash_text(const char *s) {
  char **slot = &g_text_bufs[g_text_i];
  size_t n;
  char *p;
  if (!s)
    s = "";
  n = strlen(s);
  p = (char *)realloc(*slot, n + 1);
  if (!p)
    return NULL;
  memcpy(p, s, n + 1);
  *slot = p;
  g_text_i = (g_text_i + 1) % TEXT_RING;
  return p;
}

static int frame_bytes(int width, int height, size_t *out) {
  size_t w;
  size_t h;
  if (width <= 0 || height <= 0 || !out)
    return 0;
  w = (size_t)width;
  h = (size_t)height;
  if (w > SIZE_MAX / 4)
    return 0;
  if (h > SIZE_MAX / (w * 4))
    return 0;
  *out = w * h * 4;
  return 1;
}

int sz_mobile_push_event(const SzInputEvent *event) {
  int next;
  int last;
  if (!event)
    return 0;
  pthread_mutex_lock(&g_q_lock);
  if (event->kind == SZ_INPUT_POINTER &&
      event->pointer_phase == SZ_POINTER_MOVE && g_q_head != g_q_tail) {
    last = (g_q_tail + EVENT_CAP - 1) % EVENT_CAP;
    if (g_queue[last].kind == SZ_INPUT_POINTER &&
        g_queue[last].pointer_phase == SZ_POINTER_MOVE) {
      g_queue[last] = *event;
      pthread_mutex_unlock(&g_q_lock);
      return 1;
    }
  }
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  g_queue[g_q_tail] = *event;
  g_q_tail = next;
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int sz_mobile_poll_event(SzInputEvent *out) {
  size_t n;
  char *p;
  if (!out)
    return 0;
  pthread_mutex_lock(&g_q_lock);
  if (g_motion_reported != g_motion_value) {
    g_motion_reported = g_motion_value;
    *out = (SzInputEvent){.kind = SZ_INPUT_REDUCED_MOTION,
                         .reduced_motion = g_motion_value};
    pthread_mutex_unlock(&g_q_lock);
    return 1;
  }
  if (g_q_head == g_q_tail) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  if (out->kind == SZ_INPUT_TEXT_EDIT || out->kind == SZ_INPUT_ACCESSIBILITY) {
    if (!out->text)
      out->text = "";
    n = strlen(out->text);
    p = (char *)realloc(g_poll_text, n + 1);
    if (!p) {
      pthread_mutex_unlock(&g_q_lock);
      return 0;
    }
    memcpy(p, out->text, n + 1);
    g_poll_text = p;
    out->text = g_poll_text;
  }
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int scuzz_android_push_pointer(float x, float y, int phase) {
  SzInputEvent ev;
  if (phase != SZ_POINTER_DOWN && phase != SZ_POINTER_MOVE &&
      phase != SZ_POINTER_UP)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_POINTER;
  ev.x = x;
  ev.y = y;
  ev.pointer_phase = (SzPointerPhase)phase;
  return sz_mobile_push_event(&ev);
}

static int push_text_event(SzInputEvent ev, const char *text) {
  const char *stored;
  pthread_mutex_lock(&g_q_lock);
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i])) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  stored = stash_text(text);
  if (!stored) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  ev.text = stored;
  g_queue[g_q_tail] = ev;
  g_q_tail = (g_q_tail + 1) % EVENT_CAP;
  pthread_mutex_unlock(&g_q_lock);
  return 1;
}

int scuzz_android_push_text_edit(const char *text) {
  return push_text_event((SzInputEvent){.kind = SZ_INPUT_TEXT_EDIT}, text);
}

int scuzz_android_push_accessibility(uintptr_t id, int action, const char *text,
                                      int start, int end, int64_t value) {
  SzInputEvent event = {.kind = SZ_INPUT_ACCESSIBILITY, .a11y_id = id,
    .a11y_action = action, .a11y_start = start, .a11y_end = end, .a11y_value = value};
  return push_text_event(event, text);
}

int scuzz_android_push_resize(int w, int h) {
  SzInputEvent ev;
  if (w <= 0 || h <= 0)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_RESIZE;
  ev.width = w;
  ev.height = h;
  return sz_mobile_push_event(&ev);
}

int scuzz_android_push_lifecycle(int phase) {
  SzInputEvent ev;
  if (phase != SZ_LIFECYCLE_RESUME && phase != SZ_LIFECYCLE_PAUSE &&
      phase != SZ_LIFECYCLE_STOP)
    return 0;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = (SzLifecyclePhase)phase;
  return sz_mobile_push_event(&ev);
}

int sz_mobile_present(const char *title, int point_w, int point_h, int pixel_w,
                      int pixel_h, const uint8_t *rgba, size_t nbytes) {
  size_t need;
  (void)title;
  if (!rgba || point_w <= 0 || point_h <= 0)
    return 0;
  if (!frame_bytes(pixel_w, pixel_h, &need))
    return 0;
  if (nbytes < need)
    return 0;
  pthread_mutex_lock(&g_frame_lock);
  if (need > g_rgba_cap) {
    uint8_t *next = (uint8_t *)realloc(g_rgba, need);
    if (!next) {
      pthread_mutex_unlock(&g_frame_lock);
      return 0;
    }
    g_rgba = next;
    g_rgba_cap = need;
  }
  memcpy(g_rgba, rgba, need);
  g_w = pixel_w;
  g_h = pixel_h;
  g_frames++;
  pthread_mutex_unlock(&g_frame_lock);
#ifdef __ANDROID__
  if (g_frames <= 3 || (g_frames % 120) == 0)
    __android_log_print(ANDROID_LOG_INFO, "scuzz",
                        "scuzz android: present %dx%d px=%dx%d frame=%d",
                        point_w, point_h, pixel_w, pixel_h, g_frames);
#endif
  return 1;
}

void sz_mobile_set_keyboard(int visible) {
  uint32_t previous = atomic_load(&g_keyboard_request), next;
  /* Bit zero gives visibility. Other bits identify each request. */
  do {
    next = ((previous + 2u) & ~UINT32_C(1)) | (visible != 0);
  } while (!atomic_compare_exchange_weak(&g_keyboard_request, &previous, next));
}

int scuzz_android_keyboard_request(void) { return (int)atomic_load(&g_keyboard_request); }

static int frame_int(const int *p) {
  int v;
  pthread_mutex_lock(&g_frame_lock);
  v = *p;
  pthread_mutex_unlock(&g_frame_lock);
  return v;
}

int scuzz_android_frame_width(void) { return frame_int(&g_w); }

int scuzz_android_frame_height(void) { return frame_int(&g_h); }

int scuzz_android_copy_argb(int32_t *dst, int cap) {
  size_t n;
  size_t i;
  int frames;
  const uint8_t *src;
  if (!dst || cap <= 0)
    return 0;
  pthread_mutex_lock(&g_frame_lock);
  src = g_rgba;
  if (!src || g_w <= 0 || g_h <= 0) {
    pthread_mutex_unlock(&g_frame_lock);
    return 0;
  }
  n = (size_t)g_w * (size_t)g_h;
  frames = g_frames;
  if ((size_t)cap < n) {
    pthread_mutex_unlock(&g_frame_lock);
    return 0;
  }
  for (i = 0; i < n; i++) {
    uint8_t r = src[i * 4];
    uint8_t g = src[i * 4 + 1];
    uint8_t b = src[i * 4 + 2];
    uint8_t a = src[i * 4 + 3];
    dst[i] = ((int32_t)(uint32_t)a << 24) | ((int32_t)(uint32_t)r << 16) |
             ((int32_t)(uint32_t)g << 8) | (int32_t)(uint32_t)b;
  }
  pthread_mutex_unlock(&g_frame_lock);
  return frames;
}

static char *clip_dup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    return NULL;
  n = strlen(s);
  out = (char *)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

int sz_mobile_clipboard_set(const char *text) {
  free(g_clip);
  g_clip = clip_dup(text ? text : "");
  return g_clip ? 1 : 0;
}

char *sz_mobile_clipboard_get(void) { return g_clip ? clip_dup(g_clip) : NULL; }

static void json_text(FILE *stream, const char *text) {
  fputc('"', stream);
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
    if (*p == '"' || *p == '\\') { fputc('\\', stream); fputc(*p, stream); }
    else if (*p < 32) fprintf(stream, "\\u%04x", *p);
    else fputc(*p, stream);
  }
  fputc('"', stream);
}

int sz_mobile_accessibility_available(void) { return 1; }

void sz_mobile_publish_accessibility(SzA11ySnapshot *snapshot, int width, int height) {
  char *json = NULL;
  size_t length = 0;
  FILE *stream = open_memstream(&json, &length);
  if (!stream) { sz_a11y_snapshot_free(snapshot); return; }
  fprintf(stream, "{\"width\":%d,\"height\":%d,\"nodes\":[", width, height);
  for (size_t i = 0; i < sz_a11y_snapshot_count(snapshot); i++) {
    const SzA11yNode *node = sz_a11y_snapshot_node(snapshot, i);
    fprintf(stream, "%s{\"id\":%llu,\"parent\":%llu,\"role\":%d,"
      "\"bounds\":[%.9g,%.9g,%.9g,%.9g],\"actions\":%u,"
      "\"focused\":%d,\"editable\":%d,\"multiline\":%d,"
      "\"checked\":%d,\"selected\":%d,\"expanded\":%d,"
      "\"start\":%d,\"end\":%d,\"number\":%lld,\"minimum\":%lld,"
      "\"maximum\":%lld,\"step\":%lld,\"label\":",
      i ? "," : "", (unsigned long long)node->id, (unsigned long long)node->parent_id,
      node->role, (double)node->bounds.x, (double)node->bounds.y,
      (double)node->bounds.w, (double)node->bounds.h, node->actions,
      node->focused, node->editable, node->multiline, node->checked, node->selected,
      node->expanded, node->selection_start, node->selection_end,
      (long long)node->number, (long long)node->minimum, (long long)node->maximum,
      (long long)node->step);
    json_text(stream, node->label);
    fputs(",\"value\":", stream);
    json_text(stream, node->value);
    fputs(",\"description\":", stream);
    json_text(stream, node->description);
    fputc('}', stream);
  }
  fputs("]}", stream);
  int failed = ferror(stream);
  if (fclose(stream) != 0) failed = 1;
  sz_a11y_snapshot_free(snapshot);
  if (failed) { free(json); return; }
  pthread_mutex_lock(&g_a11y_lock);
  if (!g_a11y_json || strcmp(g_a11y_json, json)) {
    free(g_a11y_json);
    g_a11y_json = json;
    g_a11y_version++;
  } else free(json);
  pthread_mutex_unlock(&g_a11y_lock);
}

/* Return an owned UTF-8 document only when the published tree changes. */
char *scuzz_android_accessibility_snapshot(uint64_t previous) {
  pthread_mutex_lock(&g_a11y_lock);
  char *result = NULL;
  if (previous != g_a11y_version) {
    const char *tree = g_a11y_json ? g_a11y_json : "{\"width\":0,\"height\":0,\"nodes\":[]}";
    size_t capacity = strlen(tree) + 64;
    result = malloc(capacity);
    if (result) snprintf(result, capacity, "{\"version\":%llu,\"tree\":%s}",
      (unsigned long long)g_a11y_version, tree);
  }
  pthread_mutex_unlock(&g_a11y_lock);
  return result;
}
