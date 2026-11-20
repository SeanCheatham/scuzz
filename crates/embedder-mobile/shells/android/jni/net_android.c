/* Android Net HTTP client transport over JNI HttpURLConnection.
 * Worker callbacks own Java and raw malloc state. The fiber owns runtime values. */

#define _POSIX_C_SOURCE 200809L

#include "net_transport.h"
#include "rt_util.h"

#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static JavaVM *g_jvm;
static jclass g_http_class;
static jmethodID g_start_mid;
static jmethodID g_cancel_mid;
static pthread_once_t g_tls_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_tls_key;

static void detach_current_thread(void *vm_ptr) {
  JavaVM *vm = (JavaVM *)vm_ptr;
  JNIEnv *env = NULL;
  if (!vm) return;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
    (*vm)->DetachCurrentThread(vm);
}

static void init_tls_key(void) {
  (void)pthread_key_create(&g_tls_key, detach_current_thread);
}

static JNIEnv *jni_get_env(void) {
  JavaVM *vm = g_jvm;
  JNIEnv *env = NULL;
  jint status;
  if (!vm) return NULL;
  status = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
  if (status == JNI_OK) return env;
  if (status != JNI_EDETACHED) return NULL;
  (void)pthread_once(&g_tls_once, init_tls_key);
#ifdef __ANDROID__
  if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
#else
  if ((*vm)->AttachCurrentThread(vm, (void **)&env, NULL) != JNI_OK) return NULL;
#endif
  (void)pthread_setspecific(g_tls_key, vm);
  return env;
}

int scuzz_android_http_init(JavaVM *vm);
void scuzz_android_http_shutdown(JavaVM *vm);
JNIEXPORT jboolean JNICALL Java_dev_scuzz_app_ScuzzHttp_nativeWriteChunk(
    JNIEnv *env, jclass cls, jlong handle, jbyteArray chunk, jint length);
JNIEXPORT void JNICALL Java_dev_scuzz_app_ScuzzHttp_nativeComplete(
    JNIEnv *env, jclass cls, jlong handle, jint status, jbyteArray headers_blob,
    jbyteArray body_bytes, jstring failure_str);

int scuzz_android_http_init(JavaVM *vm) {
  static const JNINativeMethod natives[] = {
      {"nativeWriteChunk", "(J[BI)Z",
       (void *)Java_dev_scuzz_app_ScuzzHttp_nativeWriteChunk},
      {"nativeComplete", "(JI[B[BLjava/lang/String;)V",
       (void *)Java_dev_scuzz_app_ScuzzHttp_nativeComplete},
  };
  JNIEnv *env = NULL;
  jclass local_cls;
  if (!vm) return 0;
  if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK || !env)
    return 0;
  local_cls = (*env)->FindClass(env, "dev/scuzz/app/ScuzzHttp");
  if (!local_cls || (*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    return 0;
  }
  if ((*env)->RegisterNatives(
          env, local_cls, natives, (jint)(sizeof natives / sizeof natives[0])) != 0 ||
      (*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, local_cls);
    return 0;
  }
  g_http_class = (jclass)(*env)->NewGlobalRef(env, local_cls);
  (*env)->DeleteLocalRef(env, local_cls);
  if (!g_http_class) return 0;
  g_start_mid = (*env)->GetStaticMethodID(
      env, g_http_class, "start", "(JLjava/lang/String;[B[B[BZI)Ljava/lang/Object;");
  g_cancel_mid = (*env)->GetStaticMethodID(
      env, g_http_class, "cancel", "(Ljava/lang/Object;)V");
  if (!g_start_mid || !g_cancel_mid || (*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    (*env)->DeleteGlobalRef(env, g_http_class);
    g_http_class = NULL;
    g_start_mid = NULL;
    g_cancel_mid = NULL;
    return 0;
  }
  g_jvm = vm;
  return 1;
}

void scuzz_android_http_shutdown(JavaVM *vm) {
  JNIEnv *env = NULL;
  g_jvm = NULL;
  g_start_mid = NULL;
  g_cancel_mid = NULL;
  if (vm && g_http_class &&
      (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK && env) {
    (*env)->DeleteGlobalRef(env, g_http_class);
  }
  g_http_class = NULL;
}

typedef struct AndroidHttpState {
  atomic_int refs;
  pthread_mutex_t lock;
  int read_fd;
  int write_fd;
  int stream_fd;
  int is_stream;
  int stream_complete;
  int cancelled;
  int finished;
  int status;
  int64_t stream_bytes;
  char *stream_path;
  uint8_t *resp_headers;
  size_t resp_headers_len;
  uint8_t *resp_body;
  size_t resp_body_len;
  char *failure;
  jobject call_ref;
} AndroidHttpState;

typedef struct {
  AndroidHttpState *request;
  const char *operation;
} AndroidHttpEnv;

static char *dup_cstr(const char *s) {
  size_t len;
  char *copy;
  if (!s) return NULL;
  len = strlen(s);
  copy = (char *)malloc(len + 1);
  if (copy) memcpy(copy, s, len + 1);
  return copy;
}

static AndroidHttpState *state_new(void) {
  AndroidHttpState *st = (AndroidHttpState *)calloc(1, sizeof *st);
  if (!st) return NULL;
  atomic_init(&st->refs, 1);
  pthread_mutex_init(&st->lock, NULL);
  st->read_fd = -1;
  st->write_fd = -1;
  st->stream_fd = -1;
  return st;
}

static void state_unref(AndroidHttpState *st) {
  if (!st) return;
  if (atomic_fetch_sub(&st->refs, 1) != 1) return;
  if (st->read_fd >= 0) close(st->read_fd);
  if (st->write_fd >= 0) close(st->write_fd);
  if (st->stream_fd >= 0) close(st->stream_fd);
  free(st->stream_path);
  free(st->resp_headers);
  free(st->resp_body);
  free(st->failure);
  pthread_mutex_destroy(&st->lock);
  free(st);
}

static void put_be32(uint8_t *dst, uint32_t v) {
  dst[0] = (uint8_t)(v >> 24);
  dst[1] = (uint8_t)(v >> 16);
  dst[2] = (uint8_t)(v >> 8);
  dst[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *src) {
  return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
         ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

static int encode_request_headers(SzMap *headers, uint8_t **out_buf, size_t *out_len) {
  SzList *rows = sz_map_to_list(headers);
  SzList *it;
  size_t total = 0;
  size_t off = 0;
  uint8_t *buf = NULL;
  *out_buf = NULL;
  *out_len = 0;
  for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = (SzPair *)sz_list_head(it);
    size_t klen = (size_t)sz_string_len((SzString *)kv->left);
    size_t vlen = (size_t)sz_string_len((SzString *)kv->right);
    if (klen > INT32_MAX || vlen > INT32_MAX || total > SIZE_MAX - 8 - klen - vlen) {
      sz_release(rows);
      return 0;
    }
    total += 8 + klen + vlen;
  }
  if (total > 0) {
    buf = (uint8_t *)malloc(total);
    if (!buf) {
      sz_release(rows);
      return 0;
    }
    for (it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
      SzPair *kv = (SzPair *)sz_list_head(it);
      SzString *k = (SzString *)kv->left;
      SzString *v = (SzString *)kv->right;
      size_t klen = (size_t)sz_string_len(k);
      size_t vlen = (size_t)sz_string_len(v);
      put_be32(buf + off, (uint32_t)klen);
      off += 4;
      if (klen) memcpy(buf + off, sz_string_cstr(k), klen);
      off += klen;
      put_be32(buf + off, (uint32_t)vlen);
      off += 4;
      if (vlen) memcpy(buf + off, sz_string_cstr(v), vlen);
      off += vlen;
    }
  }
  sz_release(rows);
  *out_buf = buf;
  *out_len = total;
  return 1;
}

static SzMap *decode_response_headers(const uint8_t *blob, size_t len) {
  SzMap *headers = NULL;
  size_t off = 0;
  while (blob && off + 8 <= len) {
    uint32_t klen = get_be32(blob + off);
    off += 4;
    if (klen > len - off - 4) break;
    const char *kptr = (const char *)(blob + off);
    off += klen;
    uint32_t vlen = get_be32(blob + off);
    off += 4;
    if (vlen > len - off) break;
    const char *vptr = (const char *)(blob + off);
    off += vlen;
    SzString *k = sz_string_from_bytes(kptr, klen);
    SzString *v = sz_string_from_bytes(vptr, vlen);
    SzMap *next = sz_map_set(headers, k, v, 1);
    sz_release(headers);
    sz_release(k);
    sz_release(v);
    headers = next;
  }
  return headers;
}

static jbyteArray make_byte_array(JNIEnv *env, const void *bytes, size_t len) {
  jbyteArray arr;
  if (len > (size_t)INT_MAX) return NULL;
  arr = (*env)->NewByteArray(env, (jsize)len);
  if (!arr || (*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    return NULL;
  }
  if (len > 0) {
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)len, (const jbyte *)bytes);
    if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
      return NULL;
    }
  }
  return arr;
}

static uint8_t *copy_byte_array(JNIEnv *env, jbyteArray arr, size_t *out_len) {
  jsize len;
  uint8_t *buf;
  *out_len = 0;
  if (!arr) return NULL;
  len = (*env)->GetArrayLength(env, arr);
  if (len <= 0) return NULL;
  buf = (uint8_t *)malloc((size_t)len);
  if (!buf) return NULL;
  (*env)->GetByteArrayRegion(env, arr, 0, len, (jbyte *)buf);
  if ((*env)->ExceptionCheck(env)) {
    (*env)->ExceptionClear(env);
    free(buf);
    return NULL;
  }
  *out_len = (size_t)len;
  return buf;
}

JNIEXPORT jboolean JNICALL Java_dev_scuzz_app_ScuzzHttp_nativeWriteChunk(
    JNIEnv *env, jclass cls, jlong handle, jbyteArray chunk, jint length) {
  AndroidHttpState *st = (AndroidHttpState *)(intptr_t)handle;
  uint8_t buf[16384];
  int remaining = (int)length;
  int src_off = 0;
  (void)cls;
  if (!st || !chunk || remaining < 0) return JNI_FALSE;
  while (remaining > 0) {
    int take = remaining > (int)sizeof buf ? (int)sizeof buf : remaining;
    size_t written = 0;
    (*env)->GetByteArrayRegion(env, chunk, src_off, take, (jbyte *)buf);
    if ((*env)->ExceptionCheck(env)) {
      (*env)->ExceptionClear(env);
      return JNI_FALSE;
    }
    pthread_mutex_lock(&st->lock);
    if (st->cancelled || st->stream_fd < 0 ||
        (uint64_t)take > (uint64_t)INT64_MAX - (uint64_t)st->stream_bytes) {
      pthread_mutex_unlock(&st->lock);
      return JNI_FALSE;
    }
    while (written < (size_t)take) {
      ssize_t n = write(st->stream_fd, buf + written, (size_t)take - written);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) {
        if (!st->failure) st->failure = dup_cstr("cannot write output file");
        pthread_mutex_unlock(&st->lock);
        return JNI_FALSE;
      }
      written += (size_t)n;
    }
    st->stream_bytes += take;
    pthread_mutex_unlock(&st->lock);
    src_off += take;
    remaining -= take;
  }
  return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_dev_scuzz_app_ScuzzHttp_nativeComplete(
    JNIEnv *env, jclass cls, jlong handle, jint status, jbyteArray headers_blob,
    jbyteArray body_bytes, jstring failure_str) {
  AndroidHttpState *st = (AndroidHttpState *)(intptr_t)handle;
  size_t hdr_len = 0;
  size_t body_len = 0;
  uint8_t *hdr_copy = NULL;
  uint8_t *body_copy = NULL;
  char *fail_copy = NULL;
  (void)cls;
  if (!st) return;

  hdr_copy = copy_byte_array(env, headers_blob, &hdr_len);
  body_copy = copy_byte_array(env, body_bytes, &body_len);
  if (failure_str) {
    const char *chars = (*env)->GetStringUTFChars(env, failure_str, NULL);
    if (chars) {
      fail_copy = dup_cstr(chars);
      (*env)->ReleaseStringUTFChars(env, failure_str, chars);
    } else {
      (*env)->ExceptionClear(env);
      fail_copy = dup_cstr("HTTP request failed");
    }
  }

  pthread_mutex_lock(&st->lock);
  if (!st->cancelled) {
    if (fail_copy && !st->failure) {
      st->failure = fail_copy;
      fail_copy = NULL;
    }
    st->status = (int)status;
    if (!st->failure && st->is_stream) {
      if (st->stream_fd < 0) {
        st->failure = dup_cstr("cannot write output file");
      } else if (fsync(st->stream_fd) != 0) {
        close(st->stream_fd);
        st->stream_fd = -1;
        st->failure = dup_cstr("cannot sync output file");
      } else {
        close(st->stream_fd);
        st->stream_fd = -1;
        st->stream_complete = 1;
      }
    }
    if (!st->failure) {
      st->resp_headers = hdr_copy;
      st->resp_headers_len = hdr_len;
      hdr_copy = NULL;
      st->resp_body = body_copy;
      st->resp_body_len = body_len;
      body_copy = NULL;
    }
    if (st->is_stream && st->failure) {
      if (st->stream_fd >= 0) {
        close(st->stream_fd);
        st->stream_fd = -1;
      }
      if (st->stream_path) unlink(st->stream_path);
    }
    st->finished = 1;
    if (st->write_fd >= 0) {
      char wake = 1;
      while (write(st->write_fd, &wake, 1) < 0 && errno == EINTR) {}
    }
  } else {
    if (st->stream_fd >= 0) {
      close(st->stream_fd);
      st->stream_fd = -1;
    }
    if (st->is_stream && !st->stream_complete && st->stream_path)
      unlink(st->stream_path);
  }
  pthread_mutex_unlock(&st->lock);

  free(hdr_copy);
  free(body_copy);
  free(fail_copy);
  state_unref(st);
}

static AndroidHttpState *android_http_prepare(SzPair *parameters) {
  SzString *url = (SzString *)parameters->left;
  SzPair *arguments = (SzPair *)parameters->right;
  const char *method = sz_string_cstr((SzString *)arguments->left);
  SzPair *payload = (SzPair *)arguments->right;
  SzMap *headers = (SzMap *)payload->left;
  SzString *body = (SzString *)payload->right;
  int stream = strcmp(method, "GET_TO_FILE") == 0;
  int64_t wait_ms = sz_net_http_wait_ms(headers, 30000);
  int descriptors[2];
  char host[256];
  char path[1024];
  int port = 80;
  int is_v6 = 0;
  int tls = 0;
  uint8_t *hdr_blob = NULL;
  size_t hdr_blob_len = 0;
  JNIEnv *jenv;
  AndroidHttpState *st = state_new();
  if (!st) return NULL;
  st->is_stream = stream;

  if (sz_net_parse_http_url(sz_string_cstr(url), host, sizeof host, path, sizeof path,
                            &port, &is_v6, &tls) != 1) {
    st->failure = dup_cstr("invalid URL");
    st->finished = 1;
    return st;
  }
  if (sz_net_live_replay() && !sz_net_host_is_loopback(host)) {
    st->failure = dup_cstr("live replay rejects non-loopback");
    st->finished = 1;
    return st;
  }
  if (stream) {
    if (!body || !sz_string_len(body) ||
        strlen(sz_string_cstr(body)) != (size_t)sz_string_len(body)) {
      st->failure = dup_cstr("invalid output path");
      st->finished = 1;
      return st;
    }
    st->stream_path = dup_cstr(sz_string_cstr(body));
    if (!st->stream_path) {
      st->failure = dup_cstr("cannot create output file");
      st->finished = 1;
      return st;
    }
    unlink(st->stream_path);
    st->stream_fd = open(st->stream_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (st->stream_fd < 0) {
      st->failure = dup_cstr("cannot create output file");
      st->finished = 1;
      return st;
    }
  }
  if (!encode_request_headers(headers, &hdr_blob, &hdr_blob_len)) {
    if (st->stream_fd >= 0) {
      close(st->stream_fd);
      st->stream_fd = -1;
      unlink(st->stream_path);
    }
    st->failure = dup_cstr("invalid request headers");
    st->finished = 1;
    return st;
  }

  jenv = jni_get_env();
  if (!jenv || !g_http_class || !g_start_mid) {
    free(hdr_blob);
    if (st->stream_fd >= 0) {
      close(st->stream_fd);
      st->stream_fd = -1;
      unlink(st->stream_path);
    }
    st->failure = dup_cstr("Android HTTP runtime is not initialized");
    st->finished = 1;
    return st;
  }

  if (pipe(descriptors) != 0) {
    free(hdr_blob);
    if (st->stream_fd >= 0) {
      close(st->stream_fd);
      st->stream_fd = -1;
      unlink(st->stream_path);
    }
    st->failure = dup_cstr("cannot start request");
    st->finished = 1;
    return st;
  }
  st->read_fd = descriptors[0];
  st->write_fd = descriptors[1];
  for (int i = 0; i < 2; i++) {
    if (fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(descriptors[i], F_SETFL, O_NONBLOCK) < 0) {
      free(hdr_blob);
      close(st->read_fd);
      close(st->write_fd);
      st->read_fd = st->write_fd = -1;
      if (st->stream_fd >= 0) {
        close(st->stream_fd);
        st->stream_fd = -1;
        unlink(st->stream_path);
      }
      st->failure = dup_cstr("cannot start request");
      st->finished = 1;
      return st;
    }
  }

  if ((*jenv)->PushLocalFrame(jenv, 16) < 0) {
    (*jenv)->ExceptionClear(jenv);
    free(hdr_blob);
    close(st->read_fd);
    close(st->write_fd);
    st->read_fd = st->write_fd = -1;
    if (st->stream_fd >= 0) {
      close(st->stream_fd);
      st->stream_fd = -1;
      unlink(st->stream_path);
    }
    st->failure = dup_cstr("cannot start request");
    st->finished = 1;
    return st;
  }

  {
    jstring jmethod = (*jenv)->NewStringUTF(jenv, stream ? "GET" : method);
    jbyteArray jurl = make_byte_array(jenv, sz_string_cstr(url), (size_t)sz_string_len(url));
    jbyteArray jhdrs = make_byte_array(jenv, hdr_blob ? hdr_blob : (const uint8_t *)"", hdr_blob_len);
    jbyteArray jbody = (!stream && body)
                           ? make_byte_array(jenv, sz_string_cstr(body), (size_t)sz_string_len(body))
                           : NULL;
    jobject local_call = NULL;
    free(hdr_blob);

    if (!jmethod || !jurl || !jhdrs || (!stream && body && !jbody)) {
      (*jenv)->ExceptionClear(jenv);
      (*jenv)->PopLocalFrame(jenv, NULL);
      close(st->read_fd);
      close(st->write_fd);
      st->read_fd = st->write_fd = -1;
      if (st->stream_fd >= 0) {
        close(st->stream_fd);
        st->stream_fd = -1;
        unlink(st->stream_path);
      }
      st->failure = dup_cstr("cannot start request");
      st->finished = 1;
      return st;
    }

    atomic_fetch_add(&st->refs, 1);
    local_call = (*jenv)->CallStaticObjectMethod(
        jenv, g_http_class, g_start_mid, (jlong)(intptr_t)st, jmethod, jurl, jhdrs, jbody,
        stream ? JNI_TRUE : JNI_FALSE, (jint)(wait_ms > INT_MAX ? INT_MAX : wait_ms));
    if ((*jenv)->ExceptionCheck(jenv) || !local_call) {
      (*jenv)->ExceptionClear(jenv);
      (*jenv)->PopLocalFrame(jenv, NULL);
      state_unref(st);
      close(st->read_fd);
      close(st->write_fd);
      st->read_fd = st->write_fd = -1;
      if (st->stream_fd >= 0) {
        close(st->stream_fd);
        st->stream_fd = -1;
        unlink(st->stream_path);
      }
      st->failure = dup_cstr("cannot start request");
      st->finished = 1;
      return st;
    }

    pthread_mutex_lock(&st->lock);
    st->call_ref = (*jenv)->NewGlobalRef(jenv, local_call);
    pthread_mutex_unlock(&st->lock);
    (*jenv)->PopLocalFrame(jenv, NULL);
  }
  return st;
}

static void *android_http_start(void *value) {
  SzPair *pack = (SzPair *)value;
  AndroidHttpEnv *env = (AndroidHttpEnv *)pack->right;
  env->request = android_http_prepare((SzPair *)pack->left);
  return NULL;
}

static void *android_http_close(void *value) {
  AndroidHttpEnv *env = (AndroidHttpEnv *)value;
  AndroidHttpState *st = env->request;
  jobject call_ref = NULL;
  env->request = NULL;
  if (!st) return NULL;

  pthread_mutex_lock(&st->lock);
  st->cancelled = 1;
  if (st->write_fd >= 0) close(st->write_fd);
  if (st->read_fd >= 0) close(st->read_fd);
  st->read_fd = st->write_fd = -1;
  if (st->stream_fd >= 0) {
    close(st->stream_fd);
    st->stream_fd = -1;
  }
  if (st->is_stream && !st->stream_complete && st->stream_path)
    unlink(st->stream_path);
  call_ref = st->call_ref;
  st->call_ref = NULL;
  pthread_mutex_unlock(&st->lock);

  if (call_ref) {
    JNIEnv *jenv = jni_get_env();
    if (jenv) {
      if (g_http_class && g_cancel_mid) {
        (*jenv)->CallStaticVoidMethod(jenv, g_http_class, g_cancel_mid, call_ref);
        if ((*jenv)->ExceptionCheck(jenv)) (*jenv)->ExceptionClear(jenv);
      }
      (*jenv)->DeleteGlobalRef(jenv, call_ref);
    }
  }
  state_unref(st);
  return NULL;
}

static SzIo *android_http_result(void *unused, void *value) {
  AndroidHttpEnv *env = (AndroidHttpEnv *)value;
  AndroidHttpState *st = env->request;
  char drain[16];
  (void)unused;
  if (!st) {
    char msg[128];
    snprintf(msg, sizeof msg, "%s: cannot start request", env->operation);
    return fail_drop(sz_error_new(6, msg));
  }
  if (st->read_fd >= 0) {
    while (read(st->read_fd, drain, sizeof drain) > 0) {}
  }

  pthread_mutex_lock(&st->lock);
  {
    const char *failure = st->failure;
    if (!failure && (!st->finished || st->status < 100))
      failure = "incomplete HTTP response";
    if (failure) {
      char msg[512];
      snprintf(msg, sizeof msg, "%s: %s", env->operation, failure);
      pthread_mutex_unlock(&st->lock);
      return fail_drop(sz_error_new(6, msg));
    }
  }
  {
    SzMap *headers = decode_response_headers(st->resp_headers, st->resp_headers_len);
    void *response;
    if (st->is_stream) {
      void *status = sz_box_i64(st->status);
      void *count = sz_box_i64(st->stream_bytes);
      SzPair *inner = sz_pair_new(headers, count);
      response = sz_pair_new(status, inner);
      sz_release(status);
      sz_release(count);
      sz_release(inner);
    } else {
      SzString *body = sz_string_from_bytes(
          st->resp_body ? (const char *)st->resp_body : "", st->resp_body_len);
      response = sz_net_http_resp(st->status, headers, body);
      sz_release(body);
    }
    sz_release(headers);
    pthread_mutex_unlock(&st->lock);
    return pure_drop(response);
  }
}

static SzIo *android_http_wait(void *unused, void *value) {
  AndroidHttpEnv *env = (AndroidHttpEnv *)value;
  AndroidHttpState *st = env->request;
  (void)unused;
  if (!st || st->read_fd < 0)
    return android_http_result(NULL, env);
  return fm_drop(sz_io_poll_readable(st->read_fd), android_http_result, env);
}

SzIo *sz_net_live_http_req(const char *method, SzString *url, SzMap *headers, SzString *body) {
  AndroidHttpEnv *env = (AndroidHttpEnv *)rc_box_zero(sizeof *env);
  env->operation = sz_net_http_op(method);
  SzString *name = sz_string_from_cstr(method);
  SzPair *payload = sz_pair_new(headers, body);
  SzPair *arguments = sz_pair_new(name, payload);
  SzPair *parameters = sz_pair_new(url, arguments);
  SzPair *pack = sz_pair_new(parameters, env);
  sz_release(name);
  sz_release(payload);
  sz_release(arguments);
  sz_release(parameters);
  SzIo *inner = fm_drop(sz_io_delay(android_http_start, pack), android_http_wait, env);
  SzIo *finalizer = sz_io_delay(android_http_close, env);
  SzIo *io = sz_io_ensure(inner, finalizer);
  sz_release(pack);
  sz_release(inner);
  sz_release(finalizer);
  sz_release(env);
  return io;
}

SzIo *sz_net_live_http_get_to_file(SzString *url, SzMap *headers, SzString *path) {
  AndroidHttpEnv *env = (AndroidHttpEnv *)rc_box_zero(sizeof *env);
  env->operation = "Net.httpGetToFile";
  SzString *name = sz_string_from_cstr("GET_TO_FILE");
  SzPair *payload = sz_pair_new(headers, path);
  SzPair *arguments = sz_pair_new(name, payload);
  SzPair *parameters = sz_pair_new(url, arguments);
  SzPair *pack = sz_pair_new(parameters, env);
  sz_release(name);
  sz_release(payload);
  sz_release(arguments);
  sz_release(parameters);
  SzIo *inner = fm_drop(sz_io_delay(android_http_start, pack), android_http_wait, env);
  SzIo *finalizer = sz_io_delay(android_http_close, env);
  SzIo *io = sz_io_ensure(inner, finalizer);
  sz_release(pack);
  sz_release(inner);
  sz_release(finalizer);
  sz_release(env);
  return io;
}

/* The client target has no HTTP server transport. */
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serve is unavailable on Android");
}
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnce is unavailable on Android");
}
SzIo *sz_net_serve_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveTls is unavailable on Android");
}
SzIo *sz_net_serve_once_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnceTls is unavailable on Android");
}
SzIo *sz_net_serve_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler,
                             void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveTlsFiles is unavailable on Android");
}
SzIo *sz_net_serve_once_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler,
                                  void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnceTlsFiles is unavailable on Android");
}
