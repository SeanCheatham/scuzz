#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#include "net_transport.h"
#include "rt_util.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

enum { HTTP_BODY_MAX = 1024 * 1024, HTTP_HEADERS_MAX = 16384 };

/* Delegate callbacks own only Foundation values. The fiber owns runtime values. */
@interface ScuzzHttpRequest : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) NSLock *lock;
@property(nonatomic) NSURLRequest *request;
@property(nonatomic) NSURLSession *session;
@property(nonatomic) NSURLSessionDataTask *task;
@property(nonatomic) NSHTTPURLResponse *response;
@property(nonatomic) NSMutableData *data;
@property(nonatomic) NSString *failure;
@property(nonatomic) BOOL finished;
@property(nonatomic) BOOL cancelled;
@property(nonatomic) int readFd;
@property(nonatomic) int writeFd;
- (void)start;
- (void)close;
@end

@implementation ScuzzHttpRequest
- (instancetype)init {
  self = [super init];
  if (self) {
    _lock = [NSLock new];
    _data = [NSMutableData new];
    _readFd = _writeFd = -1;
  }
  return self;
}

- (void)start {
  int descriptors[2];
  if (!self.request || pipe(descriptors) != 0) {
    self.failure = @"cannot start request";
    self.finished = YES;
    return;
  }
  self.readFd = descriptors[0];
  self.writeFd = descriptors[1];
  for (int i = 0; i < 2; i++) {
    if (fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(descriptors[i], F_SETFL, O_NONBLOCK) < 0) {
      self.failure = @"cannot start request";
      self.finished = YES;
      return;
    }
  }
  NSURLSessionConfiguration *config = NSURLSessionConfiguration.ephemeralSessionConfiguration;
  config.URLCache = nil;
  config.HTTPCookieStorage = nil;
  config.URLCredentialStorage = nil;
  config.HTTPShouldSetCookies = NO;
  config.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
  /* Idle bounds only. IO.timeout cancels the task through the finalizer. */
  config.timeoutIntervalForRequest = 30;
  config.timeoutIntervalForResource = 60;
  config.waitsForConnectivity = NO;
  NSOperationQueue *queue = [NSOperationQueue new];
  queue.maxConcurrentOperationCount = 1;
  self.session = [NSURLSession sessionWithConfiguration:config delegate:self delegateQueue:queue];
  self.task = [self.session dataTaskWithRequest:self.request];
  [self.task resume];
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
 didReceiveResponse:(NSURLResponse *)response
 completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
  (void)session;
  (void)task;
  [self.lock lock];
  if (![response isKindOfClass:NSHTTPURLResponse.class]) {
    self.failure = @"invalid HTTP response";
  } else {
    self.response = (NSHTTPURLResponse *)response;
    size_t size = 0;
    for (id key in self.response.allHeaderFields) {
      NSString *value = [self.response.allHeaderFields[key] description];
      size += [[key description] lengthOfBytesUsingEncoding:NSUTF8StringEncoding] +
              [value lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 4;
    }
    if (size > HTTP_HEADERS_MAX)
      self.failure = @"response headers too large";
    if (response.expectedContentLength > HTTP_BODY_MAX &&
        ![self.request.HTTPMethod isEqualToString:@"HEAD"])
      self.failure = @"response body too large";
  }
  BOOL reject = self.cancelled || self.failure != nil;
  [self.lock unlock];
  completion(reject ? NSURLSessionResponseCancel : NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
    didReceiveData:(NSData *)data {
  (void)session;
  [self.lock lock];
  if (!self.cancelled) {
    if (data.length > HTTP_BODY_MAX - self.data.length) {
      self.failure = @"response body too large";
      [task cancel];
    } else {
      [self.data appendData:data];
    }
  }
  [self.lock unlock];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
 willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request
 completionHandler:(void (^)(NSURLRequest *))completion {
  (void)session;
  (void)task;
  (void)response;
  (void)request;
  completion(nil);
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
 didCompleteWithError:(NSError *)error {
  (void)task;
  [self.lock lock];
  if (!self.cancelled) {
    if (error && !self.failure)
      self.failure = error.localizedDescription;
    self.finished = YES;
    if (self.writeFd >= 0) {
      char byte = 1;
      while (write(self.writeFd, &byte, 1) < 0 && errno == EINTR) {}
    }
  }
  [self.lock unlock];
  [session finishTasksAndInvalidate];
}

- (void)close {
  [self.lock lock];
  self.cancelled = YES;
  if (self.writeFd >= 0) close(self.writeFd);
  if (self.readFd >= 0) close(self.readFd);
  self.readFd = self.writeFd = -1;
  [self.lock unlock];
  [self.task cancel];
  [self.session invalidateAndCancel];
  self.task = nil;
  self.session = nil;
}
@end

typedef struct {
  void *request;
  const char *operation;
} AppleHttpEnv;

static ScuzzHttpRequest *request_state(AppleHttpEnv *env) {
  return (__bridge ScuzzHttpRequest *)env->request;
}

static ScuzzHttpRequest *apple_http_request(SzPair *parameters);

static void *apple_http_start(void *value) {
  SzPair *pack = value;
  AppleHttpEnv *env = pack->right;
  @autoreleasepool {
    ScuzzHttpRequest *state = apple_http_request(pack->left);
    env->request = (__bridge_retained void *)state;
    [state start];
  }
  return NULL;
}

static void *apple_http_close(void *value) {
  AppleHttpEnv *env = value;
  @autoreleasepool {
    ScuzzHttpRequest *request = (__bridge_transfer ScuzzHttpRequest *)env->request;
    env->request = NULL;
    [request close];
  }
  return NULL;
}

static SzIo *apple_http_result(void *unused, void *value) {
  (void)unused;
  AppleHttpEnv *env = value;
  @autoreleasepool {
    ScuzzHttpRequest *request = request_state(env);
    [request.lock lock];
    NSString *failure = request.failure;
    if (!failure && (!request.finished || !request.response))
      failure = @"incomplete HTTP response";
    if (failure) {
      NSString *message = [NSString stringWithFormat:@"%s: %@", env->operation, failure];
      SzError *error = sz_error_new(6, message.UTF8String);
      [request.lock unlock];
      return fail_drop(error);
    }
    SzMap *headers = NULL;
    for (id key in request.response.allHeaderFields) {
      NSString *name = [[key description] lowercaseString];
      NSString *value = [request.response.allHeaderFields[key] description];
      SzString *k = sz_string_from_cstr(name.UTF8String);
      SzString *v = sz_string_from_cstr(value.UTF8String);
      SzMap *next = sz_map_set(headers, k, v, 1);
      sz_release(headers);
      sz_release(k);
      sz_release(v);
      headers = next;
    }
    SzString *body = sz_string_from_bytes(request.data.bytes, request.data.length);
    void *response = sz_net_http_resp(request.response.statusCode, headers, body);
    sz_release(headers);
    sz_release(body);
    [request.lock unlock];
    return pure_drop(response);
  }
}

static SzIo *apple_http_wait(void *unused, void *value) {
  (void)unused;
  AppleHttpEnv *env = value;
  ScuzzHttpRequest *request = request_state(env);
  if (request.readFd < 0 || !request.task)
    return apple_http_result(NULL, env);
  return fm_drop(sz_io_poll_readable(request.readFd), apple_http_result, env);
}

static ScuzzHttpRequest *apple_http_request(SzPair *parameters) {
  SzString *url = parameters->left;
  SzPair *arguments = parameters->right;
  const char *method = sz_string_cstr(arguments->left);
  SzPair *payload = arguments->right;
  SzMap *headers = payload->left;
  SzString *body = payload->right;
  ScuzzHttpRequest *state = [ScuzzHttpRequest new];
  NSString *urlText = [[NSString alloc] initWithBytes:sz_string_cstr(url)
                                            length:sz_string_len(url)
                                          encoding:NSUTF8StringEncoding];
  NSURL *target = urlText ? [NSURL URLWithString:urlText] : nil;
  if (!target) {
    state.failure = @"invalid URL";
    return state;
  }
  NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:target];
  request.HTTPMethod = [NSString stringWithUTF8String:method];
  [request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
  SzList *rows = sz_map_to_list(headers);
  for (SzList *it = rows; it && !sz_list_is_empty(it); it = sz_list_tail(it)) {
    SzPair *kv = sz_list_head(it);
    NSString *name = [NSString stringWithUTF8String:sz_string_cstr(kv->left)];
    NSString *value = [NSString stringWithUTF8String:sz_string_cstr(kv->right)];
    if (!name || !value) {
      sz_release(rows);
      state.failure = @"invalid UTF-8 request headers";
      return state;
    }
    [request setValue:value forHTTPHeaderField:name];
  }
  sz_release(rows);
  if (body)
    request.HTTPBody = [NSData dataWithBytes:sz_string_cstr(body) length:sz_string_len(body)];
  state.request = request;
  return state;
}

SzIo *sz_net_live_http_req(const char *method, SzString *url, SzMap *headers, SzString *body) {
  AppleHttpEnv *env = rc_box_zero(sizeof *env);
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
  SzIo *inner = fm_drop(sz_io_delay(apple_http_start, pack), apple_http_wait, env);
  SzIo *finalizer = sz_io_delay(apple_http_close, env);
  SzIo *io = sz_io_ensure(inner, finalizer);
  sz_release(pack);
  sz_release(inner);
  sz_release(finalizer);
  sz_release(env);
  return io;
}

#if TARGET_OS_IPHONE
/* The client target has no HTTP server transport. */
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serve is unavailable on iOS");
}
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnce is unavailable on iOS");
}
SzIo *sz_net_serve_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveTls is unavailable on iOS");
}
SzIo *sz_net_serve_once_tls(int64_t port, SzCont handler, void *env) {
  (void)port;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnceTls is unavailable on iOS");
}
SzIo *sz_net_serve_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveTlsFiles is unavailable on iOS");
}
SzIo *sz_net_serve_once_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) {
  (void)port;
  (void)cert;
  (void)key;
  (void)handler;
  (void)env;
  return sz_io_fail_cstr("Net.serveOnceTlsFiles is unavailable on iOS");
}
#endif
