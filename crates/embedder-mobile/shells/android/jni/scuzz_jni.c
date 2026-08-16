/* Android JNI entry. JNI_OnLoad starts the renamed app main on a worker.
 * The shell owns process setup; scuzz_app_main mounts UiRuntime.Mobile.
 * MainActivity copies the last present frame onto a SurfaceView. */

#define _POSIX_C_SOURCE 200809L

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

extern int scuzz_app_main(int argc, char **argv);
void scuzz_android_set_alive(int alive);
int scuzz_android_frame_width(void);
int scuzz_android_frame_height(void);
int scuzz_android_frame_count(void);
int scuzz_android_copy_argb(int32_t *dst, int cap);

static void *scuzz_app_thread(void *unused) {
  char *args[] = {(char *)"scuzz", NULL};
  (void)unused;
  setenv("SCUZZ_UI_RUNTIME", "mobile", 1);
  scuzz_app_main(1, args);
  return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  pthread_t tid;
  (void)vm;
  (void)reserved;
  scuzz_android_set_alive(1);
  pthread_create(&tid, NULL, scuzz_app_thread, NULL);
  pthread_detach(tid);
  return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
  (void)vm;
  (void)reserved;
  scuzz_android_set_alive(0);
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

JNIEXPORT jint JNICALL Java_dev_scuzz_app_MainActivity_nativeFrameCount(
    JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  return scuzz_android_frame_count();
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
