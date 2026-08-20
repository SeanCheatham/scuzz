/* JNI entry. nativeStart launches the renamed app main on a worker. */

#define _POSIX_C_SOURCE 200809L

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int scuzz_app_main(int argc, char **argv);
void scuzz_android_set_alive(int alive);
void sz_mobile_shutdown(void);
int scuzz_android_frame_width(void);
int scuzz_android_frame_height(void);
int scuzz_android_copy_argb(int32_t *dst, int cap);
int scuzz_android_push_pointer(float x, float y, int phase);
int scuzz_android_push_text_edit(const char *text);
int scuzz_android_push_resize(int w, int h);
int scuzz_android_push_lifecycle(int phase);
int scuzz_android_keyboard_visible(void);

static int g_started;

static void *scuzz_app_thread(void *unused) {
  char *args[] = {(char *)"scuzz", NULL};
  (void)unused;
  scuzz_app_main(1, args);
  return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)vm;
  (void)reserved;
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  (void)vm;
  (void)reserved;
  sz_mobile_shutdown();
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativeStart(
    JNIEnv *env, jobject thiz, jstring dump_path, jint width, jint height,
    jfloat scale) {
  const char *dump;
  pthread_t tid;
  char wbuf[16];
  char hbuf[16];
  char sbuf[32];
  (void)thiz;
  if (g_started)
    return;
  g_started = 1;
  setenv("SCUZZ_UI_RUNTIME", "mobile", 1);
  if (width > 0) {
    snprintf(wbuf, sizeof wbuf, "%d", (int)width);
    setenv("SCUZZ_UI_WIDTH", wbuf, 1);
  }
  if (height > 0) {
    snprintf(hbuf, sizeof hbuf, "%d", (int)height);
    setenv("SCUZZ_UI_HEIGHT", hbuf, 1);
  }
  if (scale > 0.f) {
    snprintf(sbuf, sizeof sbuf, "%g", (double)scale);
    setenv("SCUZZ_UI_SCALE", sbuf, 1);
  }
  dump = dump_path ? (*env)->GetStringUTFChars(env, dump_path, NULL) : NULL;
  if (dump && dump[0])
    setenv("SCUZZ_UI_DEBUG_DUMP", dump, 1);
  if (dump_path && dump)
    (*env)->ReleaseStringUTFChars(env, dump_path, dump);
  scuzz_android_set_alive(1);
  if (pthread_create(&tid, NULL, scuzz_app_thread, NULL) != 0) {
    scuzz_android_set_alive(0);
    g_started = 0;
    return;
  }
  pthread_detach(tid);
}

JNIEXPORT jint JNICALL Java_dev_scuzz_app_MainActivity_nativeFrameWidth(
    JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  return scuzz_android_frame_width();
}

JNIEXPORT jint JNICALL Java_dev_scuzz_app_MainActivity_nativeFrameHeight(
    JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  return scuzz_android_frame_height();
}

JNIEXPORT jint JNICALL Java_dev_scuzz_app_MainActivity_nativeCopyFrame(
    JNIEnv *env, jobject thiz, jintArray argb) {
  jint *dst;
  jsize cap;
  int frames;
  (void)thiz;
  if (!argb)
    return 0;
  cap = (*env)->GetArrayLength(env, argb);
  dst = (*env)->GetIntArrayElements(env, argb, NULL);
  if (!dst)
    return 0;
  frames = scuzz_android_copy_argb((int32_t *)dst, (int)cap);
  (*env)->ReleaseIntArrayElements(env, argb, dst, 0);
  return frames;
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativePointer(
    JNIEnv *env, jobject thiz, jfloat x, jfloat y, jint phase) {
  (void)env;
  (void)thiz;
  (void)scuzz_android_push_pointer((float)x, (float)y, (int)phase);
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativeTextEdit(
    JNIEnv *env, jobject thiz, jstring text) {
  const char *utf;
  (void)thiz;
  if (!text) {
    (void)scuzz_android_push_text_edit("");
    return;
  }
  utf = (*env)->GetStringUTFChars(env, text, NULL);
  if (!utf) {
    (void)scuzz_android_push_text_edit("");
    return;
  }
  (void)scuzz_android_push_text_edit(utf);
  (*env)->ReleaseStringUTFChars(env, text, utf);
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativeResize(
    JNIEnv *env, jobject thiz, jint width, jint height) {
  (void)env;
  (void)thiz;
  (void)scuzz_android_push_resize((int)width, (int)height);
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativeLifecycle(
    JNIEnv *env, jobject thiz, jint phase) {
  (void)env;
  (void)thiz;
  (void)scuzz_android_push_lifecycle((int)phase);
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_MainActivity_nativeSetAlive(
    JNIEnv *env, jobject thiz, jint alive) {
  (void)env;
  (void)thiz;
  scuzz_android_set_alive((int)alive);
}

JNIEXPORT jint JNICALL Java_dev_scuzz_app_MainActivity_nativeKeyboardVisible(
    JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  return scuzz_android_keyboard_visible();
}
