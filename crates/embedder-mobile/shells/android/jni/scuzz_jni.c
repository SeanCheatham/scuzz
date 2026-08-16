/* Android JNI entry. JNI_OnLoad starts the renamed app main on a worker.
 * The shell owns process setup; scuzz_app_main mounts UiRuntime.Mobile. */

#define _POSIX_C_SOURCE 200809L

#include <jni.h>
#include <pthread.h>
#include <stdlib.h>

extern int scuzz_app_main(int argc, char **argv);
void scuzz_android_set_alive(int alive);

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
