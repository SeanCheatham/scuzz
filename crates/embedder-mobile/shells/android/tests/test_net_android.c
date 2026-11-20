#define _POSIX_C_SOURCE 200809L

#include "scuzz_rt.h"

#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int scuzz_android_http_init(JavaVM *vm);
void scuzz_android_http_shutdown(JavaVM *vm);

static int failures;

static void check(int ok, const char *name) {
  if (!ok) {
    fprintf(stderr, "Android Net proof fails: %s\n", name);
    failures++;
  }
}

static SzMap *header(SzMap *old, const char *name, const char *value) {
  SzString *k = sz_string_from_cstr(name);
  SzString *v = sz_string_from_cstr(value);
  SzMap *next = sz_map_set(old, k, v, 1);
  sz_release(k);
  sz_release(v);
  sz_release(old);
  return next;
}

static SzIo *request(const char *base, const char *path, const char *method, SzMap *headers) {
  char text[2048];
  snprintf(text, sizeof text, "%s%s", base, path);
  SzString *url = sz_string_from_cstr(text);
  SzString *body = sz_string_from_cstr("body-\xc3\xa9");
  SzIo *io = !strcmp(method, "POST")    ? sz_net_http_post(url, headers, body)
             : !strcmp(method, "PUT")   ? sz_net_http_put(url, headers, body)
             : !strcmp(method, "PATCH") ? sz_net_http_patch(url, headers, body)
             : !strcmp(method, "DELETE") ? sz_net_http_delete(url, headers)
             : !strcmp(method, "HEAD")   ? sz_net_http_head(url, headers)
                                         : sz_net_http_get(url, headers);
  sz_release(url);
  sz_release(body);
  return io;
}

static SzIoResult run(SzIo *io) { return sz_io_unsafe_run(io); }

static void drop(SzIoResult result) {
  sz_release(result.value);
  sz_release(result.error);
}

static int status(SzIoResult result, int expected) {
  return result.ok && sz_unbox_i64(((SzPair *)result.value)->left) == expected;
}

static void check_http(SzIoResult result, int expected, const char *name) {
  if (status(result, expected)) return;
  check(0, name);
  if (!result.ok && result.error && result.error->message)
    fprintf(stderr, "%s\n", sz_string_cstr(result.error->message));
}

static const char *resp_body(SzIoResult result) {
  return sz_string_cstr(((SzPair *)((SzPair *)result.value)->right)->right);
}

static SzMap *resp_headers(SzIoResult result) {
  return (SzMap *)((SzPair *)((SzPair *)result.value)->right)->left;
}

static SzIo *stream_request(const char *base, const char *path, SzMap *headers,
                            const char *output) {
  char text[2048];
  snprintf(text, sizeof text, "%s%s", base, path);
  SzString *url = sz_string_from_cstr(text);
  SzString *file = sz_string_from_cstr(output);
  SzIo *io = sz_net_http_get_to_file(url, headers, file);
  sz_release(url);
  sz_release(file);
  return io;
}

static int descriptors(void) {
  int count = 0;
  for (int fd = 0; fd < 1024; fd++) {
    if (fcntl(fd, F_GETFD) >= 0) count++;
  }
  return count;
}

static void check_local_hosts(JNIEnv *env) {
  jclass cls = (*env)->FindClass(env, "dev/scuzz/app/ScuzzHttp");
  jmethodID mid = cls ? (*env)->GetStaticMethodID(
                            env, cls, "isLocalNetworkHost", "(Ljava/lang/String;)Z")
                      : NULL;
  const char *allowed[] = {"127.0.0.1",      "::1",           "[::1]",
                           "localhost",      "10.0.2.2",      "192.168.1.42",
                           "172.20.10.2",    "169.254.10.20", "[fe80::1]",
                           "[fd12:3456::1]", "laptop.local",  "my-laptop"};
  const char *blocked[] = {"", "93.184.216.34", "8.8.8.8",
                           "[2606:2800:220:1:248:1893:25c8:1946]"};
  check(cls != NULL && mid != NULL, "lookup ScuzzHttp.isLocalNetworkHost");
  if (!cls || !mid) return;
  for (size_t i = 0; i < sizeof allowed / sizeof *allowed; i++) {
    jstring host = (*env)->NewStringUTF(env, allowed[i]);
    jboolean ok = (*env)->CallStaticBooleanMethod(env, cls, mid, host);
    check(!(*env)->ExceptionCheck(env) && ok == JNI_TRUE, allowed[i]);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, host);
  }
  for (size_t i = 0; i < sizeof blocked / sizeof *blocked; i++) {
    jstring host = (*env)->NewStringUTF(env, blocked[i]);
    jboolean ok = (*env)->CallStaticBooleanMethod(env, cls, mid, host);
    check(!(*env)->ExceptionCheck(env) && ok == JNI_FALSE, blocked[i]);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, host);
  }
  (*env)->DeleteLocalRef(env, cls);
}

typedef struct {
  const char *base;
  int ok;
} WorkerArg;

static void *native_fiber_worker(void *raw) {
  WorkerArg *arg = (WorkerArg *)raw;
  SzMap *headers = header(header(NULL, "X-Proof", "custom"), "Authorization", "Bearer sync-tok");
  int all_ok = 1;
  for (int i = 0; i < 3; i++) {
    SzIo *g1 = request(arg->base, "/echo", "GET", headers);
    SzIo *p1 = request(arg->base, "/echo", "POST", headers);
    SzIo *g2 = request(arg->base, "/echo", "PUT", headers);
    SzIo *p2 = request(arg->base, "/echo", "PATCH", headers);
    SzIo *left = sz_io_both(g1, p1);
    SzIo *right = sz_io_both(g2, p2);
    SzIo *quad = sz_io_both(left, right);
    sz_release(g1);
    sz_release(p1);
    sz_release(g2);
    sz_release(p2);
    sz_release(left);
    sz_release(right);
    SzIoResult r = run(quad);
    if (!r.ok) {
      all_ok = 0;
    } else {
      SzPair *outer = (SzPair *)r.value;
      SzPair *lpair = (SzPair *)outer->left;
      SzPair *rpair = (SzPair *)outer->right;
      if (sz_unbox_i64(((SzPair *)lpair->left)->left) != 200 ||
          sz_unbox_i64(((SzPair *)lpair->right)->left) != 200 ||
          sz_unbox_i64(((SzPair *)rpair->left)->left) != 200 ||
          sz_unbox_i64(((SzPair *)rpair->right)->left) != 200)
        all_ok = 0;
    }
    drop(r);
  }
  sz_release(headers);
  arg->ok = all_ok;
  return NULL;
}

static SzIo *noop_serve_handler(void *req, void *env) {
  (void)req;
  (void)env;
  return sz_io_pure(NULL);
}

static int run_proof(JNIEnv *jenv) {
  const char *base = getenv("SCUZZ_NET_BASE");
  const char *tls = getenv("SCUZZ_NET_TLS");
  if (!base || !tls) return 1;

  check_local_hosts(jenv);

  /* Load Java HTTP classes and worker threads before baseline checks. */
  {
    SzIoResult warm = run(request(base, "/echo", "GET", NULL));
    check_http(warm, 200, "warmup GET");
    drop(warm);
  }

  size_t heap_bytes_before = 0, heap_count_before = 0;
  uint64_t rc_before = sz_alloc_rc_sum();
  sz_alloc_stats(&heap_bytes_before, &heap_count_before);

  const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"};
  SzMap *headers = header(
      header(header(NULL, "X-Proof", "custom"), "Authorization", "Bearer sync-tok"),
      "Prefer", "wait=5");
  for (size_t i = 0; i < sizeof methods / sizeof *methods; i++) {
    SzIoResult r = run(request(base, "/echo", methods[i], headers));
    check_http(r, 200, methods[i]);
    if (r.ok) {
      check(!strcmp(resp_body(r), !strcmp(methods[i], "HEAD") ? "" : methods[i]),
            "method response body");
      SzString *mkey = sz_string_from_cstr("x-method");
      SzString *akey = sz_string_from_cstr("x-auth");
      SzString *mval = sz_map_get_or(resp_headers(r), mkey, NULL);
      SzString *aval = sz_map_get_or(resp_headers(r), akey, NULL);
      check(mval && !strcmp(sz_string_cstr(mval), methods[i]), "lowercase response header");
      check(aval && !strcmp(sz_string_cstr(aval), "Bearer sync-tok"), "bearer auth header");
      sz_release(mkey);
      sz_release(akey);
    }
    drop(r);
  }

  {
    char url_buf[2048];
    snprintf(url_buf, sizeof url_buf, "%s/empty-post", base);
    SzString *u = sz_string_from_cstr(url_buf);
    SzString *empty = sz_string_from_cstr("");
    SzIoResult r = run(sz_net_http_post(u, NULL, empty));
    sz_release(u);
    sz_release(empty);
    check_http(r, 200, "empty POST body");
    if (r.ok) check(!strcmp(resp_body(r), "empty-ok"), "empty POST payload");
    drop(r);
  }

  SzIoResult r = run(request(base, "/redirect", "GET", NULL));
  check(status(r, 302), "redirect stays visible");
  if (r.ok) {
    SzString *lkey = sz_string_from_cstr("location");
    SzString *lval = sz_map_get_or(resp_headers(r), lkey, NULL);
    check(lval && !strcmp(sz_string_cstr(lval), "/target"), "redirect location header");
    sz_release(lkey);
  }
  drop(r);

  r = run(request(base, "/unauthorized", "GET", NULL));
  check(status(r, 401), "HTTP 401 is a response");
  if (r.ok) check(!strcmp(resp_body(r), "{\"error\":\"unauthorized\"}"), "HTTP 401 body");
  drop(r);

  r = run(request(base, "/status", "GET", NULL));
  check(status(r, 503), "HTTP 503 is a response");
  drop(r);

  const char *oversized[] = {"/large", "/stream-large", "/headers-large"};
  for (size_t i = 0; i < sizeof oversized / sizeof *oversized; i++) {
    r = run(request(base, oversized[i], "GET", NULL));
    check(!r.ok && r.error && r.error->code == 6, oversized[i]);
    drop(r);
  }

  char output[] = "/tmp/scuzz-android-stream-XXXXXX";
  int output_fd = mkstemp(output);
  check(output_fd >= 0, "create stream output path");
  if (output_fd >= 0) close(output_fd);
  const char *stream_paths[] = {"/large", "/stream-large"};
  const int64_t stream_sizes[] = {1024 * 1024 + 1, 17 * 65536};
  for (size_t i = 0; i < 2; i++) {
    r = run(stream_request(base, stream_paths[i], headers, output));
    check_http(r, 200, stream_paths[i]);
    if (r.ok) {
      SzPair *inner = (SzPair *)((SzPair *)r.value)->right;
      check(sz_unbox_i64(inner->right) == stream_sizes[i], "stream byte count");
      struct stat info;
      check(stat(output, &info) == 0 && info.st_size == stream_sizes[i], "stream file size");
    }
    drop(r);
    unlink(output);
  }

  {
    int pre_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (pre_fd >= 0) close(pre_fd);
    SzIo *inner = stream_request(base, "/slow", NULL, output);
    SzIo *bounded = sz_io_timeout(30, inner);
    sz_release(inner);
    r = run(bounded);
    check(!r.ok && access(output, F_OK) != 0, "cancelled stream removes output file");
    drop(r);
    unlink(output);
  }

  r = run(stream_request(base, "/echo", NULL, ""));
  check(!r.ok && r.error && r.error->code == 6, "reject empty stream path");
  drop(r);
  r = run(stream_request(base, "/echo", NULL, "/nonexistent-scuzz-dir/out.bin"));
  check(!r.ok && r.error && r.error->code == 6, "reject unwritable stream path");
  drop(r);

  SzMap *bad = header(NULL, "Host", "wrong");
  r = run(request(base, "/forbidden", "GET", bad));
  check(!r.ok && r.error && r.error->code == 6, "reject reserved header");
  drop(r);
  sz_release(bad);

  bad = header(header(NULL, "X-Proof", "one"), "x-proof", "two");
  r = run(request(base, "/forbidden", "GET", bad));
  check(!r.ok && r.error && r.error->code == 6, "reject case duplicate header");
  drop(r);
  sz_release(bad);

  r = run(request("http://bad host", "/", "GET", NULL));
  check(!r.ok && r.error && r.error->code == 6, "reject invalid URL");
  drop(r);

  r = run(request("http://93.184.216.34", "/forbidden", "GET", NULL));
  check(!r.ok && r.error && r.error->code == 6 &&
            strstr(sz_string_cstr(r.error->message), "local network host") != NULL,
        "reject non-local cleartext HTTP");
  drop(r);

  SzIo *reused = request(base, "/echo", "GET", headers);
  for (int i = 0; i < 3; i++) {
    sz_retain(reused);
    r = run(reused);
    check(status(r, 200), "reuse request IO");
    drop(r);
  }
  sz_release(reused);
  sz_release(headers);

  for (int i = 0; i < 4; i++) {
    pthread_t thread;
    WorkerArg arg = {base, 0};
    if (pthread_create(&thread, NULL, native_fiber_worker, &arg) == 0) {
      pthread_join(thread, NULL);
      check(arg.ok == 1, "native pthread JNI multi-fiber HTTP");
    } else {
      check(0, "spawn native fiber pthread");
    }
  }

  int before = descriptors();
  for (int i = 0; i < 16; i++) {
    SzIo *inner = request(base, "/slow", "GET", NULL);
    SzIo *bounded = sz_io_timeout(30, inner);
    sz_release(inner);
    r = run(bounded);
    check(!r.ok, "request cancellation");
    drop(r);
  }
  r = run(sz_io_sleep_ms(100));
  drop(r);
  check(descriptors() <= before + 4, "cancellation closes request descriptors");

  {
    SzString *dummy = sz_string_from_cstr("cert.pem");
    SzIo *s1 = sz_net_serve(8080, noop_serve_handler, NULL);
    SzIo *s2 = sz_net_serve_once(8080, noop_serve_handler, NULL);
    SzIo *s3 = sz_net_serve_tls(8080, noop_serve_handler, NULL);
    SzIo *s4 = sz_net_serve_once_tls(8080, noop_serve_handler, NULL);
    SzIo *s5 = sz_net_serve_tls_files(8080, dummy, dummy, noop_serve_handler, NULL);
    SzIo *s6 = sz_net_serve_once_tls_files(8080, dummy, dummy, noop_serve_handler, NULL);
    sz_release(dummy);
    SzIo *servers[] = {s1, s2, s3, s4, s5, s6};
    for (size_t i = 0; i < 6; i++) {
      r = run(servers[i]);
      check(!r.ok && r.error && strstr(sz_string_cstr(r.error->message), "unavailable on Android"),
            "Android server stub");
      drop(r);
    }
  }

  r = run(request(tls, "/echo", "GET", NULL));
  if (getenv("SCUZZ_NET_TRUSTED") != NULL)
    check_http(r, 200, "platform certificate trust on loopback");
  else
    check(!r.ok && r.error && r.error->code == 6, "reject untrusted loopback certificate");
  drop(r);

  size_t heap_bytes_after = 0, heap_count_after = 0;
  uint64_t rc_after = sz_alloc_rc_sum();
  sz_alloc_stats(&heap_bytes_after, &heap_count_after);
  check(heap_bytes_after == heap_bytes_before && heap_count_after == heap_count_before &&
            rc_after == rc_before,
        "runtime heap and RC counts return to baseline");

  if (!failures) {
    puts("Android Net proof ok");
    fflush(stdout);
  }
  return failures ? 1 : 0;
}

int main(void) {
  const char *classpath = getenv("SCUZZ_JAVA_CLASSPATH");
  const char *truststore = getenv("SCUZZ_NET_TRUSTSTORE");
  char cp_opt[2048];
  char ts_opt[2048];
  JavaVMOption opts[8];
  JavaVMInitArgs vm_args;
  JavaVM *vm = NULL;
  JNIEnv *env = NULL;
  int nopts = 0;

  if (!classpath || !classpath[0]) {
    fprintf(stderr, "missing SCUZZ_JAVA_CLASSPATH\n");
    return 1;
  }
  snprintf(cp_opt, sizeof cp_opt, "-Djava.class.path=%s", classpath);
  opts[nopts++].optionString = cp_opt;
  opts[nopts++].optionString = (char *)"--add-opens=java.base/java.net=ALL-UNNAMED";
  opts[nopts++].optionString =
      (char *)"--add-opens=java.base/sun.net.www.protocol.http=ALL-UNNAMED";
  opts[nopts++].optionString =
      (char *)"--add-opens=java.base/sun.net.www.protocol.https=ALL-UNNAMED";
  if (truststore && truststore[0]) {
    snprintf(ts_opt, sizeof ts_opt, "-Djavax.net.ssl.trustStore=%s", truststore);
    opts[nopts++].optionString = ts_opt;
    opts[nopts++].optionString = (char *)"-Djavax.net.ssl.trustStorePassword=changeit";
    opts[nopts++].optionString = (char *)"-Djavax.net.ssl.trustStoreType=PKCS12";
  }

  memset(&vm_args, 0, sizeof vm_args);
  vm_args.version = JNI_VERSION_1_6;
  vm_args.nOptions = nopts;
  vm_args.options = opts;
  vm_args.ignoreUnrecognized = JNI_TRUE;

  if (JNI_CreateJavaVM(&vm, (void **)&env, &vm_args) != JNI_OK || !vm || !env) {
    fprintf(stderr, "JNI_CreateJavaVM failed\n");
    return 1;
  }
  if (!scuzz_android_http_init(vm)) {
    fprintf(stderr, "scuzz_android_http_init failed\n");
    (*vm)->DestroyJavaVM(vm);
    return 1;
  }

  int status_code = run_proof(env);
  scuzz_android_http_shutdown(vm);
  (*vm)->DestroyJavaVM(vm);
  return status_code;
}
