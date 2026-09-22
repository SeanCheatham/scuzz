#ifndef SCUZZ_NET_TRANSPORT_H
#define SCUZZ_NET_TRANSPORT_H

#include "scuzz_rt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

/* Shared live-socket helpers for the HTTP and Net backends. */
static inline int set_nonblock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl < 0)
    return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static inline int tcp_begin(const struct sockaddr *sa, socklen_t len) {
  int fd;
  if (!sa || len == 0)
    return -1;
  fd = socket(sa->sa_family, SOCK_STREAM, 0);
  if (fd < 0 || set_nonblock(fd) != 0) {
    if (fd >= 0)
      close(fd);
    return -1;
  }
#ifdef SO_NOSIGPIPE
  {
    int nosig = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosig, sizeof nosig);
  }
#endif
  if (connect(fd, sa, len) != 0 && errno != EINPROGRESS && errno != EAGAIN) {
    int err = errno;
    close(fd);
    errno = err;
    return -1;
  }
  return fd;
}

/* SCUZZ_NET_LIVE with TestRuntime keeps the live sockets. Loopback only. */
static inline int sz_net_live_replay(void) {
  const char *live = getenv("SCUZZ_NET_LIVE");
  const char *tr = getenv("SCUZZ_TESTRT");
  return live && live[0] == '1' && tr && tr[0] == '1' && !sz_testrt_net_is_fake();
}

/* Live transport runs after shared validation and simulation dispatch. */
SzIo *sz_net_live_http_req(const char *method, SzString *url, SzMap *headers,
                         SzString *body);
const char *sz_net_http_op(const char *method);
int sz_net_http_header_valid(const char *name, const char *value);
int sz_net_http_header_skip(const char *name);
int sz_net_http_name_equal(const char *a, const char *b);
int sz_net_host_equal(const char *a, const char *b);

static inline int sz_net_host_is_loopback(const char *host) {
  return sz_net_host_equal(host, "127.0.0.1") || sz_net_host_equal(host, "::1") ||
         sz_net_host_equal(host, "localhost");
}

#endif
