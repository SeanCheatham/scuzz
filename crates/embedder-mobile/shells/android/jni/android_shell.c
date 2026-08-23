/* Android sz_mobile_* shell. Strong defs override the weak runtime stubs. */

#include "scuzz_mobile.h"

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 64

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static pthread_mutex_t g_q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_alive;
static int g_frames;
static int g_w;
static int g_h;
static _Atomic int g_keyboard;
static uint8_t *g_rgba;
static size_t g_rgba_cap;
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;
static char g_poll_text[TEXT_LEN];
static JavaVM *g_vm;
static char *g_clip;

void scuzz_android_set_vm(JavaVM *vm) { g_vm = vm; }

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return atomic_load(&g_alive); }

void scuzz_android_set_alive(int alive) { atomic_store(&g_alive, alive ? 1 : 0); }

void sz_mobile_shutdown(void) {
  pthread_mutex_lock(&g_frame_lock);
  free(g_rgba);
  g_rgba = NULL;
  g_rgba_cap = 0;
  g_w = g_h = 0;
  pthread_mutex_unlock(&g_frame_lock);
  free(g_clip);
  g_clip = NULL;
  scuzz_android_set_alive(0);
}

static int q_full(void) { return ((g_q_tail + 1) % EVENT_CAP) == g_q_head; }

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    if (g_queue[i].kind == SZ_INPUT_TEXT_EDIT && g_queue[i].text == slot)
      return 1;
  }
  return 0;
}

static void copy_text(char *dst, const char *s) {
  size_t n;
  if (!s)
    s = "";
  n = strlen(s);
  if (n >= TEXT_LEN)
    n = TEXT_LEN - 1;
  memcpy(dst, s, n);
  dst[n] = '\0';
}

static const char *stash_text(const char *s) {
  char *dst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  copy_text(dst, s);
  return dst;
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
  if (!event)
    return 0;
  pthread_mutex_lock(&g_q_lock);
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
  if (!out)
    return 0;
  pthread_mutex_lock(&g_q_lock);
  if (g_q_head == g_q_tail) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  if (out->kind == SZ_INPUT_TEXT_EDIT) {
    copy_text(g_poll_text, out->text);
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

int scuzz_android_push_text_edit(const char *text) {
  SzInputEvent ev;
  pthread_mutex_lock(&g_q_lock);
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i])) {
    pthread_mutex_unlock(&g_q_lock);
    return 0;
  }
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text);
  g_queue[g_q_tail] = ev;
  g_q_tail = (g_q_tail + 1) % EVENT_CAP;
  pthread_mutex_unlock(&g_q_lock);
  return 1;
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
  if (g_frames <= 3 || (g_frames % 120) == 0)
    __android_log_print(ANDROID_LOG_INFO, "scuzz",
                        "scuzz android: present %dx%d px=%dx%d frame=%d",
                        point_w, point_h, pixel_w, pixel_h, g_frames);
  return 1;
}

void sz_mobile_set_keyboard(int visible) {
  atomic_store(&g_keyboard, visible ? 1 : 0);
}

int scuzz_android_keyboard_visible(void) { return atomic_load(&g_keyboard); }

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

static JNIEnv *android_env(int *need_detach) {
  JNIEnv *env = NULL;
  *need_detach = 0;
  if (!g_vm)
    return NULL;
  if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
    return env;
  if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == 0) {
    *need_detach = 1;
    return env;
  }
  return NULL;
}

static void android_clear_exn(JNIEnv *env) {
  if (env && (*env)->ExceptionCheck(env))
    (*env)->ExceptionClear(env);
}

static jobject android_clipboard(JNIEnv *env) {
  jclass at;
  jmethodID current;
  jobject app;
  jclass ctx;
  jmethodID get_sys;
  jstring name;
  jobject cm;
  if (!env)
    return NULL;
  at = (*env)->FindClass(env, "android/app/ActivityThread");
  if (!at) {
    android_clear_exn(env);
    return NULL;
  }
  current = (*env)->GetStaticMethodID(env, at, "currentApplication",
                                      "()Landroid/app/Application;");
  if (!current) {
    android_clear_exn(env);
    return NULL;
  }
  app = (*env)->CallStaticObjectMethod(env, at, current);
  android_clear_exn(env);
  if (!app)
    return NULL;
  ctx = (*env)->GetObjectClass(env, app);
  get_sys = (*env)->GetMethodID(env, ctx, "getSystemService",
                               "(Ljava/lang/String;)Ljava/lang/Object;");
  if (!get_sys) {
    android_clear_exn(env);
    return NULL;
  }
  name = (*env)->NewStringUTF(env, "clipboard");
  cm = (*env)->CallObjectMethod(env, app, get_sys, name);
  android_clear_exn(env);
  return cm;
}

int sz_mobile_clipboard_set(const char *text) {
  int detach = 0;
  JNIEnv *env;
  jobject cm;
  free(g_clip);
  g_clip = clip_dup(text ? text : "");
  env = android_env(&detach);
  cm = android_clipboard(env);
  if (env && cm) {
    jclass clip_cls = (*env)->FindClass(env, "android/content/ClipData");
    jmethodID neu;
    jstring label;
    jstring body;
    jobject clip;
    jclass cm_cls;
    jmethodID set_clip;
    if (clip_cls) {
      neu = (*env)->GetStaticMethodID(
          env, clip_cls, "newPlainText",
          "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/"
          "ClipData;");
      label = (*env)->NewStringUTF(env, "scuzz");
      body = (*env)->NewStringUTF(env, text ? text : "");
      if (neu) {
        clip = (*env)->CallStaticObjectMethod(env, clip_cls, neu, label, body);
        android_clear_exn(env);
        cm_cls = (*env)->GetObjectClass(env, cm);
        set_clip = (*env)->GetMethodID(env, cm_cls, "setPrimaryClip",
                                       "(Landroid/content/ClipData;)V");
        if (set_clip && clip)
          (*env)->CallVoidMethod(env, cm, set_clip, clip);
        android_clear_exn(env);
      }
    }
  }
  if (detach && g_vm)
    (*g_vm)->DetachCurrentThread(g_vm);
  return g_clip ? 1 : 0;
}

char *sz_mobile_clipboard_get(void) {
  int detach = 0;
  JNIEnv *env = android_env(&detach);
  jobject cm = android_clipboard(env);
  char *out = NULL;
  if (env && cm) {
    jclass cm_cls = (*env)->GetObjectClass(env, cm);
    jmethodID get_clip = (*env)->GetMethodID(
        env, cm_cls, "getPrimaryClip", "()Landroid/content/ClipData;");
    jobject clip = get_clip ? (*env)->CallObjectMethod(env, cm, get_clip) : NULL;
    android_clear_exn(env);
    if (clip) {
      jclass clip_cls = (*env)->GetObjectClass(env, clip);
      jmethodID get_item = (*env)->GetMethodID(
          env, clip_cls, "getItemAt", "(I)Landroid/content/ClipData$Item;");
      jobject item =
          get_item ? (*env)->CallObjectMethod(env, clip, get_item, 0) : NULL;
      android_clear_exn(env);
      if (item) {
        jclass item_cls = (*env)->GetObjectClass(env, item);
        jmethodID get_text = (*env)->GetMethodID(
            env, item_cls, "getText", "()Ljava/lang/CharSequence;");
        jobject cs =
            get_text ? (*env)->CallObjectMethod(env, item, get_text) : NULL;
        android_clear_exn(env);
        if (cs) {
          jmethodID cs_str = (*env)->GetMethodID(
              env, (*env)->GetObjectClass(env, cs), "toString",
              "()Ljava/lang/String;");
          jstring js = cs_str
                           ? (jstring)(*env)->CallObjectMethod(env, cs, cs_str)
                           : NULL;
          const char *utf;
          android_clear_exn(env);
          utf = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
          if (utf) {
            out = clip_dup(utf);
            (*env)->ReleaseStringUTFChars(env, js, utf);
          }
        }
      }
    }
  }
  if (detach && g_vm)
    (*g_vm)->DetachCurrentThread(g_vm);
  if (!out && g_clip)
    out = clip_dup(g_clip);
  return out;
}
