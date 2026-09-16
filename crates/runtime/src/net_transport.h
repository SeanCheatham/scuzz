#ifndef SCUZZ_NET_TRANSPORT_H
#define SCUZZ_NET_TRANSPORT_H

#include "scuzz_rt.h"

/* Live transport runs after shared validation and simulation dispatch. */
SzIo *sz_net_live_http_req(const char *method, SzString *url, SzMap *headers,
                         SzString *body);
const char *sz_net_http_op(const char *method);
int sz_net_http_header_valid(const char *name, const char *value);
int sz_net_http_header_skip(const char *name);
int sz_net_http_name_equal(const char *a, const char *b);
int sz_net_host_equal(const char *a, const char *b);

#endif
