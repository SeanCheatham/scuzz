// The browser has no HTTP client sockets and no listener. These Net effects fail loud at the call.
#include "scuzz_rt.h"

static SzIo *no_net(void) { return sz_io_fail_cstr("Net: not available on web"); }

SzIo *sz_net_http_get(SzString *url, SzMap *headers) { (void)url; (void)headers; return no_net(); }
SzIo *sz_net_http_post(SzString *url, SzMap *headers, SzString *body) { (void)url; (void)headers; (void)body; return no_net(); }
SzIo *sz_net_http_put(SzString *url, SzMap *headers, SzString *body) { (void)url; (void)headers; (void)body; return no_net(); }
SzIo *sz_net_http_patch(SzString *url, SzMap *headers, SzString *body) { (void)url; (void)headers; (void)body; return no_net(); }
SzIo *sz_net_http_delete(SzString *url, SzMap *headers) { (void)url; (void)headers; return no_net(); }
SzIo *sz_net_http_head(SzString *url, SzMap *headers) { (void)url; (void)headers; return no_net(); }
SzIo *sz_net_serve_once(int64_t port, SzCont handler, void *env) { (void)port; (void)handler; (void)env; return no_net(); }
SzIo *sz_net_serve(int64_t port, SzCont handler, void *env) { (void)port; (void)handler; (void)env; return no_net(); }
SzIo *sz_net_serve_once_tls(int64_t port, SzCont handler, void *env) { (void)port; (void)handler; (void)env; return no_net(); }
SzIo *sz_net_serve_tls(int64_t port, SzCont handler, void *env) { (void)port; (void)handler; (void)env; return no_net(); }
SzIo *sz_net_serve_once_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) { (void)port; (void)cert; (void)key; (void)handler; (void)env; return no_net(); }
SzIo *sz_net_serve_tls_files(int64_t port, SzString *cert, SzString *key, SzCont handler, void *env) { (void)port; (void)cert; (void)key; (void)handler; (void)env; return no_net(); }
