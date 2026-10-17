#define _POSIX_C_SOURCE 200809L
#include "scuzz_embedder.h"
#include <dbus/dbus.h>
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NAME "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define SETTINGS "org.freedesktop.portal.Settings"
#define SPACE "org.freedesktop.appearance"

static DBusConnection *connect_service(void) {
  DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
  assert(bus);
  dbus_connection_set_exit_on_disconnect(bus, FALSE);
  assert(dbus_bus_request_name(bus, NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) ==
         DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
  return bus;
}

static void close_bus(DBusConnection *bus) {
  dbus_connection_close(bus);
  dbus_connection_unref(bus);
}

static void append_value(DBusMessageIter *iter, int malformed, dbus_uint32_t value) {
  DBusMessageIter variant;
  const char *text = "invalid";
  assert(dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                          malformed ? "s" : "u", &variant));
  if (malformed)
    assert(dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &text));
  else
    assert(dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value));
  assert(dbus_message_iter_close_container(iter, &variant));
}

static void signal_value(DBusConnection *bus, int command, dbus_uint32_t value) {
  DBusMessage *message = dbus_message_new_signal(command == 'p' ? "/wrong" : PATH,
                                                 SETTINGS, "SettingChanged");
  DBusMessageIter iter;
  const char *space = command == 'n' ? "other.namespace" : SPACE;
  const char *key = "reduced-motion";
  assert(message);
  dbus_message_iter_init_append(message, &iter);
  assert(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &space));
  assert(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key));
  append_value(&iter, command == 'x', value);
  assert(dbus_connection_send(bus, message, NULL));
  dbus_message_unref(message);
  dbus_connection_flush(bus);
}

static void service(int fd) {
  DBusConnection *bus = connect_service();
  dbus_uint32_t value = 1;
  int stale = 0;
  char command = '!';
  assert(write(fd, &command, 1) == 1);
  for (;;) {
    struct pollfd ready = {.fd = fd, .events = POLLIN};
    DBusMessage *message;
    assert(poll(&ready, 1, 0) >= 0);
    if (ready.revents & (POLLHUP | POLLERR)) break;
    if (ready.revents & POLLIN) {
      assert(read(fd, &command, 1) == 1);
      if (command == 'q') break;
      if (command == 'r' || command == 't') {
        close_bus(bus);
        bus = connect_service();
        value = 0;
        stale = command == 't';
      } else if (command == 's') {
        DBusConnection *other = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
        assert(other);
        signal_value(other, '0', 0);
        close_bus(other);
      } else {
        if (command >= '0' && command <= '2') value = command - '0';
        signal_value(bus, command, value);
      }
      assert(write(fd, &command, 1) == 1);
    }
    dbus_connection_read_write(bus, 1);
    while ((message = dbus_connection_pop_message(bus))) {
      if (dbus_message_is_method_call(message, SETTINGS, "ReadOne")) {
        const char *space, *key;
        DBusMessageIter iter;
        DBusMessage *reply;
        dbus_uint32_t read_value = value;
        assert(dbus_message_has_path(message, PATH));
        assert(dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &space,
                 DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID));
        assert(!strcmp(space, SPACE) && !strcmp(key, "reduced-motion"));
        if (stale) {
          value = 1;
          signal_value(bus, '1', value);
          stale = 0;
        }
        reply = dbus_message_new_method_return(message);
        assert(reply);
        dbus_message_iter_init_append(reply, &iter);
        append_value(&iter, 0, read_value);
        assert(dbus_connection_send(bus, reply, NULL));
        dbus_message_unref(reply);
        dbus_connection_flush(bus);
      }
      dbus_message_unref(message);
    }
  }
  close_bus(bus);
  close(fd);
  _exit(0);
}

static void command(int fd, char value) {
  struct pollfd ready = {.fd = fd, .events = POLLIN};
  char reply;
  assert(write(fd, &value, 1) == 1);
  assert(poll(&ready, 1, 1000) == 1);
  assert(read(fd, &reply, 1) == 1 && reply == value);
}

static void expect_change(int reduced) {
  struct timespec wait = {.tv_nsec = 1000000};
  for (int i = 0; i < 1000; i++) {
    SzInputEvent event;
    if (sz_embedder_poll_event(&event)) {
      assert(event.kind == SZ_INPUT_REDUCED_MOTION);
      assert(event.reduced_motion == reduced);
      assert(sz_embedder_reduced_motion() == reduced);
      return;
    }
    nanosleep(&wait, NULL);
  }
  assert(!"motion preference event is missing");
}

static void expect_quiet(void) {
  struct timespec wait = {.tv_nsec = 1000000};
  for (int i = 0; i < 50; i++) {
    SzInputEvent event;
    assert(!sz_embedder_poll_event(&event));
    nanosleep(&wait, NULL);
  }
}

int main(void) {
  int sockets[2], status;
  pid_t child;
  char ready;
  assert(getenv("DBUS_SESSION_BUS_ADDRESS"));
  assert(!sz_embedder_reduced_motion());
  sz_embedder_shutdown();
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  child = fork();
  assert(child >= 0);
  if (!child) {
    close(sockets[0]);
    service(sockets[1]);
  }
  close(sockets[1]);
  struct pollfd service_ready = {.fd = sockets[0], .events = POLLIN};
  assert(poll(&service_ready, 1, 1000) == 1);
  assert(read(sockets[0], &ready, 1) == 1 && ready == '!');
  assert(sz_embedder_reduced_motion() == 1);
  expect_quiet();
  for (int i = 0; i < 8; i++) {
    command(sockets[0], '0'); expect_change(0);
    command(sockets[0], '1'); expect_change(1);
  }
  for (const char *p = "xnp1s"; *p; p++) {
    command(sockets[0], *p); expect_quiet();
    assert(sz_embedder_reduced_motion() == 1);
  }
  command(sockets[0], '2'); expect_change(0);
  command(sockets[0], '1'); expect_change(1);
  command(sockets[0], 'r'); expect_change(0);
  command(sockets[0], '1'); expect_change(1);
  command(sockets[0], 't'); expect_quiet();
  assert(sz_embedder_reduced_motion() == 1);
  sz_embedder_shutdown();
  assert(sz_embedder_reduced_motion() == 1);
  expect_quiet();
  sz_embedder_shutdown();
  assert(write(sockets[0], "q", 1) == 1);
  close(sockets[0]);
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  puts("Native motion: initial value, changes, invalid signals, restart, and shutdown pass.");
  return 0;
}
