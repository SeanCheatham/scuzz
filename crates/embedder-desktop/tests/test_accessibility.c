#define _POSIX_C_SOURCE 200809L

#include "scuzz_embedder.h"

void sz_ui_script_run_file(SzUiSession *session, const char *path);

#include <assert.h>
#include <dbus/dbus.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUS_SERVICE "org.a11y.Bus"
#define BUS_PATH "/org/a11y/bus"
#define BUS_IFACE "org.a11y.Bus"
#define REGISTRY_SERVICE "org.a11y.atspi.Registry"
#define SOCKET_IFACE "org.a11y.atspi.Socket"
#define TEST_IFACE "org.scuzz.TestRegistry"
#define ROOT_PATH "/org/a11y/atspi/accessible/root"
#define DESKTOP_PATH "/org/a11y/atspi/accessible/desktop"
#define CACHE_PATH "/org/a11y/atspi/cache"
#define NULL_PATH "/org/a11y/atspi/null"

#define IFACE_PROPS "org.freedesktop.DBus.Properties"
#define IFACE_ACCESSIBLE "org.a11y.atspi.Accessible"
#define IFACE_APPLICATION "org.a11y.atspi.Application"
#define IFACE_COMPONENT "org.a11y.atspi.Component"
#define IFACE_ACTION "org.a11y.atspi.Action"
#define IFACE_VALUE "org.a11y.atspi.Value"
#define IFACE_TEXT "org.a11y.atspi.Text"
#define IFACE_EDITABLE "org.a11y.atspi.EditableText"
#define IFACE_CACHE "org.a11y.atspi.Cache"

#define SAME_COUNT 100

enum {
  STATE_ACTIVE = 1,
  STATE_CHECKED = 4,
  STATE_DEFUNCT = 6,
  STATE_EDITABLE = 7,
  STATE_ENABLED = 8,
  STATE_FOCUSABLE = 11,
  STATE_FOCUSED = 12,
  STATE_HAS_TOOLTIP = 13,
  STATE_MODAL = 16,
  STATE_MULTI_LINE = 17,
  STATE_SENSITIVE = 24,
  STATE_SHOWING = 25,
  STATE_SINGLE_LINE = 26,
  STATE_VISIBLE = 30,
  STATE_SELECTABLE_TEXT = 38,
  STATE_CHECKABLE = 41
};

static int has_state(const dbus_uint32_t words[2], int bit) {
  if (bit < 32)
    return (words[0] & (1u << bit)) != 0;
  return (words[1] & (1u << (bit - 32))) != 0;
}

/* Mock org.a11y.Bus and org.a11y.atspi.Registry service process. */
static void run_mock_a11y_daemon(int ctl_fd) {
  const char *bus_addr = getenv("DBUS_SESSION_BUS_ADDRESS");
  DBusConnection *conn;
  char embedded_bus[128] = "";
  char embedded_path[128] = "";
  int owns_bus = 1;
  char ready = '!';

  assert(bus_addr && bus_addr[0]);
  conn = dbus_connection_open_private(bus_addr, NULL);
  assert(conn);
  dbus_connection_set_exit_on_disconnect(conn, FALSE);
  assert(dbus_bus_register(conn, NULL));
  assert(dbus_bus_request_name(conn, BUS_SERVICE, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                               NULL) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
  assert(dbus_bus_request_name(conn, REGISTRY_SERVICE,
                               DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) ==
         DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
  assert(write(ctl_fd, &ready, 1) == 1);

  for (;;) {
    struct pollfd pfd = {.fd = ctl_fd, .events = POLLIN};
    DBusMessage *msg;
    assert(poll(&pfd, 1, 0) >= 0);
    if (pfd.revents & (POLLHUP | POLLERR))
      break;
    if (pfd.revents & POLLIN) {
      char cmd = 0;
      assert(read(ctl_fd, &cmd, 1) == 1);
      if (cmd == 'q')
        break;
      if (cmd == 'd' && owns_bus) {
        (void)dbus_bus_release_name(conn, BUS_SERVICE, NULL);
        owns_bus = 0;
      }
      assert(write(ctl_fd, &cmd, 1) == 1);
    }
    (void)dbus_connection_read_write(conn, 1);
    while ((msg = dbus_connection_pop_message(conn)) != NULL) {
      if (owns_bus &&
          dbus_message_is_method_call(msg, BUS_IFACE, "GetAddress") &&
          dbus_message_has_path(msg, BUS_PATH)) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        assert(reply);
        assert(dbus_message_append_args(reply, DBUS_TYPE_STRING, &bus_addr,
                                        DBUS_TYPE_INVALID));
        assert(dbus_connection_send(conn, reply, NULL));
        dbus_message_unref(reply);
        dbus_connection_flush(conn);
      } else if (dbus_message_is_method_call(msg, SOCKET_IFACE, "Embed") &&
                 dbus_message_has_path(msg, ROOT_PATH)) {
        DBusMessageIter args, st, out, rst;
        const char *abus = NULL, *apath = NULL;
        const char *self_unique = dbus_bus_get_unique_name(conn);
        const char *dpath = DESKTOP_PATH;
        DBusMessage *reply;
        assert(dbus_message_iter_init(msg, &args) &&
               dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRUCT);
        dbus_message_iter_recurse(&args, &st);
        assert(dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_STRING);
        dbus_message_iter_get_basic(&st, &abus);
        assert(dbus_message_iter_next(&st) &&
               dbus_message_iter_get_arg_type(&st) == DBUS_TYPE_OBJECT_PATH);
        dbus_message_iter_get_basic(&st, &apath);
        snprintf(embedded_bus, sizeof(embedded_bus), "%s", abus ? abus : "");
        snprintf(embedded_path, sizeof(embedded_path), "%s",
                 apath ? apath : "");
        reply = dbus_message_new_method_return(msg);
        assert(reply);
        dbus_message_iter_init_append(reply, &out);
        assert(dbus_message_iter_open_container(&out, DBUS_TYPE_STRUCT, NULL,
                                                &rst));
        assert(
            dbus_message_iter_append_basic(&rst, DBUS_TYPE_STRING, &self_unique));
        assert(dbus_message_iter_append_basic(&rst, DBUS_TYPE_OBJECT_PATH,
                                              &dpath));
        assert(dbus_message_iter_close_container(&out, &rst));
        assert(dbus_connection_send(conn, reply, NULL));
        dbus_message_unref(reply);
        dbus_connection_flush(conn);
      } else if (dbus_message_is_method_call(msg, TEST_IFACE, "GetEmbedded") &&
                 dbus_message_has_path(msg, ROOT_PATH)) {
        const char *eb = embedded_bus;
        const char *ep = embedded_path;
        const char *ru = dbus_bus_get_unique_name(conn);
        DBusMessage *reply = dbus_message_new_method_return(msg);
        assert(reply);
        assert(dbus_message_append_args(reply, DBUS_TYPE_STRING, &eb,
                                        DBUS_TYPE_STRING, &ep, DBUS_TYPE_STRING,
                                        &ru, DBUS_TYPE_INVALID));
        assert(dbus_connection_send(conn, reply, NULL));
        dbus_message_unref(reply);
        dbus_connection_flush(conn);
      }
      dbus_message_unref(msg);
    }
  }
  dbus_connection_close(conn);
  dbus_connection_unref(conn);
  close(ctl_fd);
  _exit(0);
}

static void daemon_cmd(int fd, char c) {
  struct pollfd pfd = {.fd = fd, .events = POLLIN};
  char ack = 0;
  assert(write(fd, &c, 1) == 1);
  assert(poll(&pfd, 1, 2000) == 1);
  assert(read(fd, &ack, 1) == 1 && ack == c);
}

static DBusMessage *atspi_call(SzUiSession *session, DBusConnection *client,
                               DBusMessage *req) {
  DBusPendingCall *pending = NULL;
  DBusMessage *reply;
  int spins = 0;
  assert(dbus_connection_send_with_reply(client, req, &pending, 2000));
  assert(pending);
  dbus_connection_flush(client);
  while (!dbus_pending_call_get_completed(pending)) {
    if (session) {
      assert(sz_ui_pump_sync(session));
    } else {
      SzInputEvent ev;
      (void)sz_embedder_poll_event(&ev);
    }
    (void)dbus_connection_read_write_dispatch(client, 1);
    assert(++spins < 2000);
  }
  reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);
  assert(reply);
  if (session)
    assert(sz_ui_pump_sync(session));
  return reply;
}

static void call_prop_get_str(SzUiSession *session, DBusConnection *client,
                              const char *bus, const char *path,
                              const char *iface, const char *prop, char *out,
                              size_t cap) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_PROPS, "Get");
  DBusMessage *rep;
  DBusMessageIter iter, var;
  const char *s = NULL;
  assert(req);
  assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &iface,
                                  DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
  assert(dbus_message_iter_init(rep, &iter) &&
         dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT);
  dbus_message_iter_recurse(&iter, &var);
  assert(dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING);
  dbus_message_iter_get_basic(&var, &s);
  snprintf(out, cap, "%s", s ? s : "");
  dbus_message_unref(rep);
}

static dbus_int32_t call_prop_get_i32(SzUiSession *session,
                                      DBusConnection *client, const char *bus,
                                      const char *path, const char *iface,
                                      const char *prop) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_PROPS, "Get");
  DBusMessage *rep;
  DBusMessageIter iter, var;
  dbus_int32_t v = 0;
  assert(req);
  assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &iface,
                                  DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
  assert(dbus_message_iter_init(rep, &iter) &&
         dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT);
  dbus_message_iter_recurse(&iter, &var);
  assert(dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&var, &v);
  dbus_message_unref(rep);
  return v;
}

static double call_prop_get_f64(SzUiSession *session, DBusConnection *client,
                                const char *bus, const char *path,
                                const char *iface, const char *prop) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_PROPS, "Get");
  DBusMessage *rep;
  DBusMessageIter iter, var;
  double v = 0.0;
  assert(req);
  assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &iface,
                                  DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
  assert(dbus_message_iter_init(rep, &iter) &&
         dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT);
  dbus_message_iter_recurse(&iter, &var);
  assert(dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_DOUBLE);
  dbus_message_iter_get_basic(&var, &v);
  dbus_message_unref(rep);
  return v;
}

static int call_prop_set_f64(SzUiSession *session, DBusConnection *client,
                             const char *bus, const char *path,
                             const char *iface, const char *prop, double val) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_PROPS, "Set");
  DBusMessage *rep;
  DBusMessageIter args, var;
  int ok;
  assert(req);
  dbus_message_iter_init_append(req, &args);
  assert(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &iface));
  assert(dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &prop));
  assert(dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "d", &var));
  assert(dbus_message_iter_append_basic(&var, DBUS_TYPE_DOUBLE, &val));
  assert(dbus_message_iter_close_container(&args, &var));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  ok = (dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
  dbus_message_unref(rep);
  return ok;
}

static dbus_uint32_t call_get_role(SzUiSession *session, DBusConnection *client,
                                   const char *bus, const char *path,
                                   char *role_name, size_t cap) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_ACCESSIBLE, "GetRole");
  DBusMessage *rep;
  dbus_uint32_t r = 0;
  const char *rn = NULL;
  assert(req);
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_UINT32, &r,
                               DBUS_TYPE_INVALID));
  dbus_message_unref(rep);
  if (role_name && cap > 0) {
    req = dbus_message_new_method_call(bus, path, IFACE_ACCESSIBLE,
                                       "GetRoleName");
    assert(req);
    rep = atspi_call(session, client, req);
    dbus_message_unref(req);
    assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_STRING, &rn,
                                 DBUS_TYPE_INVALID));
    snprintf(role_name, cap, "%s", rn ? rn : "");
    dbus_message_unref(rep);
  }
  return r;
}

static void call_get_state(SzUiSession *session, DBusConnection *client,
                           const char *bus, const char *path,
                           dbus_uint32_t words[2]) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_ACCESSIBLE, "GetState");
  DBusMessage *rep;
  DBusMessageIter iter, arr;
  assert(req);
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_iter_init(rep, &iter) &&
         dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY);
  dbus_message_iter_recurse(&iter, &arr);
  assert(dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_UINT32);
  dbus_message_iter_get_basic(&arr, &words[0]);
  assert(dbus_message_iter_next(&arr) &&
         dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_UINT32);
  dbus_message_iter_get_basic(&arr, &words[1]);
  dbus_message_unref(rep);
}

static void call_get_extents(SzUiSession *session, DBusConnection *client,
                             const char *bus, const char *path,
                             dbus_uint32_t coord_type, dbus_int32_t *x,
                             dbus_int32_t *y, dbus_int32_t *w,
                             dbus_int32_t *h) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_COMPONENT, "GetExtents");
  DBusMessage *rep;
  DBusMessageIter iter, st;
  assert(req);
  assert(dbus_message_append_args(req, DBUS_TYPE_UINT32, &coord_type,
                                  DBUS_TYPE_INVALID));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(dbus_message_iter_init(rep, &iter) &&
         dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRUCT);
  dbus_message_iter_recurse(&iter, &st);
  dbus_message_iter_get_basic(&st, x);
  assert(dbus_message_iter_next(&st));
  dbus_message_iter_get_basic(&st, y);
  assert(dbus_message_iter_next(&st));
  dbus_message_iter_get_basic(&st, w);
  assert(dbus_message_iter_next(&st));
  dbus_message_iter_get_basic(&st, h);
  dbus_message_unref(rep);
}

static int call_do_action(SzUiSession *session, DBusConnection *client,
                          const char *bus, const char *path,
                          dbus_int32_t index) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_ACTION, "DoAction");
  DBusMessage *rep;
  dbus_bool_t ok = FALSE;
  assert(req);
  assert(
      dbus_message_append_args(req, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID));
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(
      dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok, DBUS_TYPE_INVALID));
  dbus_message_unref(rep);
  return ok != FALSE;
}

static int find_action_index(SzUiSession *session, DBusConnection *client,
                             const char *bus, const char *path,
                             const char *want) {
  dbus_int32_t n =
      call_prop_get_i32(session, client, bus, path, IFACE_ACTION, "NActions");
  for (dbus_int32_t i = 0; i < n; i++) {
    DBusMessage *req =
        dbus_message_new_method_call(bus, path, IFACE_ACTION, "GetName");
    DBusMessage *rep;
    const char *got = NULL;
    int match;
    assert(req);
    assert(
        dbus_message_append_args(req, DBUS_TYPE_INT32, &i, DBUS_TYPE_INVALID));
    rep = atspi_call(session, client, req);
    dbus_message_unref(req);
    assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_STRING, &got,
                                 DBUS_TYPE_INVALID));
    match = (got && strcmp(got, want) == 0);
    dbus_message_unref(rep);
    if (match)
      return (int)i;
  }
  return -1;
}

static int call_grab_focus(SzUiSession *session, DBusConnection *client,
                           const char *bus, const char *path) {
  DBusMessage *req =
      dbus_message_new_method_call(bus, path, IFACE_COMPONENT, "GrabFocus");
  DBusMessage *rep;
  dbus_bool_t ok = FALSE;
  assert(req);
  rep = atspi_call(session, client, req);
  dbus_message_unref(req);
  assert(
      dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok, DBUS_TYPE_INVALID));
  dbus_message_unref(rep);
  return ok != FALSE;
}

typedef struct Fixture Fixture;

typedef struct {
  Fixture *fx;
  int index;
} SameTapCtx;

struct Fixture {
  SzSignalStr *field_str;
  SzSignalStr *editor_str;
  SzSignalInt *check_sig;
  SzSignalInt *switch_sig;
  SzSignalInt *radio_sig;
  SzSignalInt *slider_sig;
  SzSignalInt *big_slider_sig;
  SzSignalInt *progress_sig;
  SzSignalInt *modal_open_sig;
  SzSignalInt *modal_hits_sig;
  SzSignalInt *ephemeral_hits_sig;
  SzSignalInt *replacement_hits_sig;
  SzSignalInt *same_total_sig;
  SzSignalInt *same_last_sig;
  int same_hits[SAME_COUNT];
  SameTapCtx same_ctx[SAME_COUNT];

  SzView *root;
  SzView *field;
  SzView *editor;
  SzView *checkbox;
  SzView *sw;
  SzView *radio_basic;
  SzView *radio_pro;
  SzView *slider;
  SzView *big_slider;
  SzView *progress;
  SzView *slot_box;
  SzView *ephemeral_btn;
  SzView *replacement_btn;
  SzView *replace_trigger_btn;
  SzView *open_modal_btn;
  SzView *same_grid;
  SzView *same_btns[SAME_COUNT];
  SzView *clip_scroll;
  SzView *clip_partial_btn;
  SzView *clip_hidden_btn;
  SzView *overlay;
  SzView *modal_confirm_btn;
  SzView *modal_close_btn;
};

static void on_same_tap(SzView *view, void *env) {
  SameTapCtx *ctx = (SameTapCtx *)env;
  (void)view;
  ctx->fx->same_hits[ctx->index]++;
  sz_signal_int_set(ctx->fx->same_total_sig,
                    sz_signal_int_get(ctx->fx->same_total_sig) + 1);
  sz_signal_int_set(ctx->fx->same_last_sig, (int64_t)ctx->index);
}

static void on_inc_sig(SzView *view, void *env) {
  SzSignalInt *sig = (SzSignalInt *)env;
  (void)view;
  sz_signal_int_set(sig, sz_signal_int_get(sig) + 1);
}

static void on_open_modal(SzView *view, void *env) {
  Fixture *fx = (Fixture *)env;
  (void)view;
  sz_signal_int_set(fx->modal_open_sig, 1);
}

static void on_close_modal(SzView *view, void *env) {
  Fixture *fx = (Fixture *)env;
  (void)view;
  sz_signal_int_set(fx->modal_open_sig, 0);
}

static void on_replace_control(SzView *view, void *env) {
  Fixture *fx = (Fixture *)env;
  (void)view;
  sz_view_clear_children(fx->slot_box);
  fx->ephemeral_btn = NULL;
  fx->replacement_btn =
      sz_view_button("Replacement", on_inc_sig, fx->replacement_hits_sig);
  sz_view_add_child(fx->slot_box, fx->replacement_btn);
}

static void fixture_init(Fixture *fx) {
  SzView *main_col, *controls_row, *radio_grp, *clip_inner, *modal_col;
  memset(fx, 0, sizeof(*fx));
  fx->field_str = sz_signal_str("init");
  fx->editor_str = sz_signal_str("alpha\nbeta");
  fx->check_sig = sz_signal_int(0);
  fx->switch_sig = sz_signal_int(0);
  fx->radio_sig = sz_signal_int(0);
  fx->slider_sig = sz_signal_int(20);
  fx->big_slider_sig = sz_signal_int(0);
  fx->progress_sig = sz_signal_int(45);
  fx->modal_open_sig = sz_signal_int(0);
  fx->modal_hits_sig = sz_signal_int(0);
  fx->ephemeral_hits_sig = sz_signal_int(0);
  fx->replacement_hits_sig = sz_signal_int(0);
  fx->same_total_sig = sz_signal_int(0);
  fx->same_last_sig = sz_signal_int(-1);

  fx->root = sz_view_stack();
  main_col = sz_view_column();

  fx->field = sz_view_text_field(fx->field_str, "UserName");
  sz_view_add_child(main_col,
                    sz_view_tooltip("Enter account handle", fx->field));

  fx->editor = sz_view_editor(fx->editor_str);
  sz_view_add_child(main_col, sz_view_sized(360, 56, fx->editor));

  controls_row = sz_view_row();
  fx->checkbox = sz_view_checkbox(fx->check_sig, "Agree");
  fx->sw = sz_view_switch(fx->switch_sig, "Alerts");
  sz_view_add_child(controls_row, fx->checkbox);
  sz_view_add_child(controls_row, fx->sw);
  sz_view_add_child(main_col, controls_row);

  radio_grp = sz_view_radio_group("Plan", sz_view_row());
  fx->radio_basic = sz_view_radio(fx->radio_sig, 0, "Basic");
  fx->radio_pro = sz_view_radio(fx->radio_sig, 1, "Pro");
  sz_view_add_child(main_col, radio_grp);
  sz_view_add_child(main_col, fx->radio_basic);
  sz_view_add_child(main_col, fx->radio_pro);

  fx->slider = sz_view_slider(fx->slider_sig, "Volume", 0, 100, 5);
  sz_view_add_child(main_col, fx->slider);

  fx->big_slider = sz_view_slider(fx->big_slider_sig, "BigRange",
                                  -9000000000000LL, 9000000000000LL,
                                  1000000000000LL);
  sz_view_add_child(main_col, fx->big_slider);

  fx->progress = sz_view_progress(fx->progress_sig, "Upload");
  sz_view_add_child(main_col, fx->progress);

  fx->slot_box = sz_view_row();
  fx->ephemeral_btn =
      sz_view_button("Ephemeral", on_inc_sig, fx->ephemeral_hits_sig);
  sz_view_add_child(fx->slot_box, fx->ephemeral_btn);
  sz_view_add_child(main_col, fx->slot_box);

  {
    SzView *act_row = sz_view_row();
    fx->replace_trigger_btn =
        sz_view_button("Replace Control", on_replace_control, fx);
    fx->open_modal_btn = sz_view_button("Open Modal", on_open_modal, fx);
    sz_view_add_child(act_row, fx->replace_trigger_btn);
    sz_view_add_child(act_row, fx->open_modal_btn);
    sz_view_add_child(main_col, act_row);
  }

  clip_inner = sz_view_column();
  fx->clip_partial_btn =
      sz_view_button("Clipped", on_inc_sig, fx->modal_hits_sig);
  fx->clip_hidden_btn =
      sz_view_button("ScrolledOut", on_inc_sig, fx->modal_hits_sig);
  sz_view_add_child(clip_inner, sz_view_sized(100, 50, fx->clip_partial_btn));
  sz_view_add_child(clip_inner, sz_view_sized(100, 40, fx->clip_hidden_btn));
  fx->clip_scroll = sz_view_scroll(clip_inner);
  sz_view_add_child(main_col, sz_view_sized(120, 30, fx->clip_scroll));

  fx->same_grid = sz_view_grid(10);
  for (int i = 0; i < SAME_COUNT; i++) {
    fx->same_ctx[i].fx = fx;
    fx->same_ctx[i].index = i;
    fx->same_btns[i] = sz_view_button("Same", on_same_tap, &fx->same_ctx[i]);
    sz_view_add_child(fx->same_grid, fx->same_btns[i]);
  }
  sz_view_add_child(main_col, fx->same_grid);
  sz_view_add_child(fx->root, main_col);

  modal_col = sz_view_column();
  fx->modal_confirm_btn =
      sz_view_button("Confirm Modal", on_inc_sig, fx->modal_hits_sig);
  fx->modal_close_btn = sz_view_button("Close Modal", on_close_modal, fx);
  sz_view_add_child(modal_col, fx->modal_confirm_btn);
  sz_view_add_child(modal_col, fx->modal_close_btn);
  fx->overlay = sz_view_overlay(fx->modal_open_sig, "Modal Sheet", modal_col);
  sz_view_add_child(fx->root, fx->overlay);
}

static void fixture_free_signals(Fixture *fx) {
  sz_signal_str_free(fx->field_str);
  sz_signal_str_free(fx->editor_str);
  sz_signal_int_free(fx->check_sig);
  sz_signal_int_free(fx->switch_sig);
  sz_signal_int_free(fx->radio_sig);
  sz_signal_int_free(fx->slider_sig);
  sz_signal_int_free(fx->big_slider_sig);
  sz_signal_int_free(fx->progress_sig);
  sz_signal_int_free(fx->modal_open_sig);
  sz_signal_int_free(fx->modal_hits_sig);
  sz_signal_int_free(fx->ephemeral_hits_sig);
  sz_signal_int_free(fx->replacement_hits_sig);
  sz_signal_int_free(fx->same_total_sig);
  sz_signal_int_free(fx->same_last_sig);
}

static char *dump_json_str(SzView *root) {
  char *buf = NULL;
  size_t len = 0;
  FILE *f = open_memstream(&buf, &len);
  assert(f);
  sz_view_a11y_dump_json(root, f);
  fclose(f);
  assert(buf);
  return buf;
}

static char *read_all_file(const char *path) {
  FILE *f = fopen(path, "rb");
  long sz;
  char *buf;
  assert(f);
  assert(fseek(f, 0, SEEK_END) == 0);
  sz = ftell(f);
  assert(sz >= 0);
  assert(fseek(f, 0, SEEK_SET) == 0);
  buf = (char *)malloc((size_t)sz + 1);
  assert(buf);
  if (sz > 0)
    assert(fread(buf, 1, (size_t)sz, f) == (size_t)sz);
  buf[sz] = '\0';
  fclose(f);
  return buf;
}

static void make_node_path(const SzView *view, char *buf, size_t cap) {
  snprintf(buf, cap, "/org/a11y/atspi/accessible/%llu",
           (unsigned long long)sz_view_id(view));
}

int main(void) {
  int socks[2], status;
  pid_t daemon_pid;
  char ready = 0;
  char tmp_dir[] = "/tmp/scuzz-atspi-test-XXXXXX";
  char record_path[256];

  assert(getenv("DBUS_SESSION_BUS_ADDRESS"));

  unsetenv("AT_SPI_BUS_ADDRESS");
  assert(sz_embedder_accessibility_available() == 0);
  sz_embedder_shutdown();

  assert(mkdtemp(tmp_dir) != NULL);
  snprintf(record_path, sizeof(record_path), "%s/record.json", tmp_dir);

  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == 0);
  daemon_pid = fork();
  assert(daemon_pid >= 0);
  if (daemon_pid == 0) {
    close(socks[0]);
    run_mock_a11y_daemon(socks[1]);
  }
  close(socks[1]);
  {
    struct pollfd pfd = {.fd = socks[0], .events = POLLIN};
    assert(poll(&pfd, 1, 2000) == 1);
    assert(read(socks[0], &ready, 1) == 1 && ready == '!');
  }

  /* Test org.a11y.Bus lookup, AT-SPI2 interfaces, and Headless replay parity. */
  Fixture desk;
  fixture_init(&desk);
  SzUiConfig desk_cfg = {.kind = SZ_UI_RUNTIME_DESKTOP,
                         .width = 960,
                         .height = 1280,
                         .scale = 1.0,
                         .title = "AT-SPI2 Proof"};
  SzUiSession *desk_session = sz_ui_mount(&desk_cfg, desk.root);
  assert(desk_session);
  sz_ui_session_take_root(desk_session);
  assert(sz_ui_session_set_record(desk_session, record_path));
  assert(sz_ui_pump_sync(desk_session));
  assert(sz_embedder_accessibility_available() == 1);

  DBusConnection *client =
      dbus_connection_open_private(getenv("DBUS_SESSION_BUS_ADDRESS"), NULL);
  assert(client);
  dbus_connection_set_exit_on_disconnect(client, FALSE);
  assert(dbus_bus_register(client, NULL));

  char app_bus[128] = "";
  char app_root[128] = "";
  char reg_unique[128] = "";
  {
    DBusMessage *req = dbus_message_new_method_call(
        REGISTRY_SERVICE, ROOT_PATH, TEST_IFACE, "GetEmbedded");
    DBusMessage *rep;
    const char *eb = NULL, *ep = NULL, *ru = NULL;
    assert(req);
    rep = dbus_connection_send_with_reply_and_block(client, req, 1000, NULL);
    dbus_message_unref(req);
    assert(rep);
    assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_STRING, &eb,
                                 DBUS_TYPE_STRING, &ep, DBUS_TYPE_STRING, &ru,
                                 DBUS_TYPE_INVALID));
    snprintf(app_bus, sizeof(app_bus), "%s", eb);
    snprintf(app_root, sizeof(app_root), "%s", ep);
    snprintf(reg_unique, sizeof(reg_unique), "%s", ru);
    dbus_message_unref(rep);
  }
  assert(app_bus[0] != '\0');
  assert(strcmp(app_root, ROOT_PATH) == 0);

  /* Check Application root properties, role, parent plug, and Cache.GetItems. */
  {
    char rname[64] = "", tk[64] = "";
    dbus_uint32_t words[2];
    dbus_int32_t rx, ry, rw, rh;
    assert(call_get_role(desk_session, client, app_bus, ROOT_PATH, rname,
                         sizeof(rname)) == 75u);
    assert(strcmp(rname, "application") == 0);
    call_prop_get_str(desk_session, client, app_bus, ROOT_PATH,
                      IFACE_APPLICATION, "ToolkitName", tk, sizeof(tk));
    assert(strcmp(tk, "scuzz") == 0);
    call_get_state(desk_session, client, app_bus, ROOT_PATH, words);
    assert(has_state(words, STATE_ACTIVE) && has_state(words, STATE_VISIBLE) &&
           has_state(words, STATE_SHOWING));
    call_get_extents(desk_session, client, app_bus, ROOT_PATH, 1, &rx, &ry, &rw,
                     &rh);
    assert(rx == 0 && ry == 0 && rw == 960 && rh == 1280);

    DBusMessage *req =
        dbus_message_new_method_call(app_bus, ROOT_PATH, IFACE_PROPS, "Get");
    const char *ifa = IFACE_ACCESSIBLE, *prp = "Parent";
    DBusMessage *rep;
    DBusMessageIter it, var, st;
    const char *pbus = NULL, *ppath = NULL;
    assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &ifa,
                                    DBUS_TYPE_STRING, &prp, DBUS_TYPE_INVALID));
    rep = atspi_call(desk_session, client, req);
    dbus_message_unref(req);
    assert(dbus_message_iter_init(rep, &it));
    dbus_message_iter_recurse(&it, &var);
    dbus_message_iter_recurse(&var, &st);
    dbus_message_iter_get_basic(&st, &pbus);
    assert(dbus_message_iter_next(&st));
    dbus_message_iter_get_basic(&st, &ppath);
    assert(strcmp(pbus, reg_unique) == 0 && strcmp(ppath, DESKTOP_PATH) == 0);
    dbus_message_unref(rep);

    req = dbus_message_new_method_call(app_bus, CACHE_PATH, IFACE_CACHE,
                                       "GetItems");
    rep = atspi_call(desk_session, client, req);
    dbus_message_unref(req);
    assert(dbus_message_iter_init(rep, &it) &&
           dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY);
    assert(dbus_message_iter_get_element_count(&it) >= SAME_COUNT + 10);
    dbus_message_unref(rep);
  }

  /* Check 100 repeated-label "Same" controls: distinct paths, bounds, roles, and targeted actions. */
  {
    DBusMessage *req = dbus_message_new_method_call(
        app_bus, ROOT_PATH, IFACE_ACCESSIBLE, "GetChildren");
    DBusMessage *rep = atspi_call(desk_session, client, req);
    DBusMessageIter it, arr;
    char same_paths[SAME_COUNT][96];
    int same_found = 0;
    dbus_message_unref(req);
    assert(dbus_message_iter_init(rep, &it) &&
           dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY);
    dbus_message_iter_recurse(&it, &arr);
    while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
      DBusMessageIter st;
      const char *cbus = NULL, *cpath = NULL;
      char cname[64] = "";
      dbus_message_iter_recurse(&arr, &st);
      dbus_message_iter_get_basic(&st, &cbus);
      assert(dbus_message_iter_next(&st));
      dbus_message_iter_get_basic(&st, &cpath);
      call_prop_get_str(desk_session, client, app_bus, cpath, IFACE_ACCESSIBLE,
                        "Name", cname, sizeof(cname));
      if (strcmp(cname, "Same") == 0) {
        assert(same_found < SAME_COUNT);
        snprintf(same_paths[same_found], sizeof(same_paths[0]), "%s", cpath);
        for (int prev = 0; prev < same_found; prev++)
          assert(strcmp(same_paths[prev], cpath) != 0);
        same_found++;
      }
      dbus_message_iter_next(&arr);
    }
    dbus_message_unref(rep);
    assert(same_found == SAME_COUNT);

    for (int i = 0; i < SAME_COUNT; i++) {
      char expected_path[96], rname[64];
      dbus_int32_t x, y, w, h;
      make_node_path(desk.same_btns[i], expected_path, sizeof(expected_path));
      assert(strcmp(same_paths[i], expected_path) == 0);
      assert(call_get_role(desk_session, client, app_bus, same_paths[i], rname,
                           sizeof(rname)) == 43u);
      assert(strcmp(rname, "push button") == 0);
      call_get_extents(desk_session, client, app_bus, same_paths[i], 1, &x, &y,
                       &w, &h);
      assert(w > 0 && h > 0);
      if (i % 10 > 0) {
        dbus_int32_t px, py, pw, ph;
        call_get_extents(desk_session, client, app_bus, same_paths[i - 1], 1,
                         &px, &py, &pw, &ph);
        assert(x > px && y == py);
      }
    }

    /* Hit test via Component.GetAccessibleAtPoint on button #57. */
    {
      dbus_int32_t bx, by, bw, bh;
      dbus_uint32_t ct = 1;
      call_get_extents(desk_session, client, app_bus, same_paths[57], 1, &bx,
                       &by, &bw, &bh);
      dbus_int32_t cx = bx + bw / 2, cy = by + bh / 2;
      req = dbus_message_new_method_call(app_bus, ROOT_PATH, IFACE_COMPONENT,
                                         "GetAccessibleAtPoint");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &cx,
                                      DBUS_TYPE_INT32, &cy, DBUS_TYPE_UINT32,
                                      &ct, DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      DBusMessageIter hit_it, hit_st;
      const char *hbus = NULL, *hpath = NULL;
      assert(dbus_message_iter_init(rep, &hit_it));
      dbus_message_iter_recurse(&hit_it, &hit_st);
      dbus_message_iter_get_basic(&hit_st, &hbus);
      assert(dbus_message_iter_next(&hit_st));
      dbus_message_iter_get_basic(&hit_st, &hpath);
      assert(strcmp(hpath, same_paths[57]) == 0);
      dbus_message_unref(rep);
    }

    /* Activate specific repeated-label buttons #0, #42, and #99. */
    const int targets[4] = {0, 42, 99, 42};
    for (int t = 0; t < 4; t++)
      assert(
          call_do_action(desk_session, client, app_bus, same_paths[targets[t]], 0));
    assert(sz_signal_int_get(desk.same_total_sig) == 4);
    assert(sz_signal_int_get(desk.same_last_sig) == 42);
    assert(desk.same_hits[0] == 1 && desk.same_hits[42] == 2 &&
           desk.same_hits[99] == 1);
    for (int i = 0; i < SAME_COUNT; i++) {
      if (i != 0 && i != 42 && i != 99)
        assert(desk.same_hits[i] == 0);
    }
  }

  /* Check clipped scroll bounds: partial button is clipped to 30px height, scrolled-out button is absent. */
  {
    char partial_path[96], hidden_path[96], scroll_path[96];
    dbus_int32_t sx, sy, sw, sh, px, py, pw, ph, rel_x, rel_y, rel_w, rel_h;
    dbus_uint32_t words[2];
    make_node_path(desk.clip_scroll, scroll_path, sizeof(scroll_path));
    make_node_path(desk.clip_partial_btn, partial_path, sizeof(partial_path));
    make_node_path(desk.clip_hidden_btn, hidden_path, sizeof(hidden_path));

    call_get_extents(desk_session, client, app_bus, scroll_path, 1, &sx, &sy,
                     &sw, &sh);
    assert(sw == 120 && sh == 30);
    call_get_extents(desk_session, client, app_bus, partial_path, 1, &px, &py,
                     &pw, &ph);
    assert(pw > 0 && ph > 0 && ph <= 30);
    call_get_extents(desk_session, client, app_bus, partial_path, 2, &rel_x,
                     &rel_y, &rel_w, &rel_h);
    assert(rel_x == px - sx && rel_y == py - sy);

    call_get_state(desk_session, client, app_bus, hidden_path, words);
    assert(has_state(words, STATE_DEFUNCT));
    assert(!call_do_action(desk_session, client, app_bus, hidden_path, 0));
  }

  /* Check roles, states, and actions on checkbox, switch, and radio controls. */
  {
    char check_path[96], sw_path[96], rb_path[96], rp_path[96], rname[64];
    dbus_uint32_t words[2];
    make_node_path(desk.checkbox, check_path, sizeof(check_path));
    make_node_path(desk.sw, sw_path, sizeof(sw_path));
    make_node_path(desk.radio_basic, rb_path, sizeof(rb_path));
    make_node_path(desk.radio_pro, rp_path, sizeof(rp_path));

    assert(call_get_role(desk_session, client, app_bus, check_path, rname,
                         sizeof(rname)) == 7u);
    assert(strcmp(rname, "check box") == 0);
    call_get_state(desk_session, client, app_bus, check_path, words);
    assert(has_state(words, STATE_CHECKABLE) && !has_state(words, STATE_CHECKED));
    assert(call_do_action(desk_session, client, app_bus, check_path, 0));
    assert(sz_signal_int_get(desk.check_sig) == 1);
    call_get_state(desk_session, client, app_bus, check_path, words);
    assert(has_state(words, STATE_CHECKED));

    assert(call_get_role(desk_session, client, app_bus, sw_path, rname,
                         sizeof(rname)) == 62u);
    assert(call_do_action(desk_session, client, app_bus, sw_path, 0));
    assert(sz_signal_int_get(desk.switch_sig) == 1);

    assert(call_get_role(desk_session, client, app_bus, rp_path, rname,
                         sizeof(rname)) == 44u);
    call_get_state(desk_session, client, app_bus, rb_path, words);
    assert(has_state(words, STATE_CHECKED));
    assert(call_do_action(desk_session, client, app_bus, rp_path, 0));
    assert(sz_signal_int_get(desk.radio_sig) == 1);
    call_get_state(desk_session, client, app_bus, rp_path, words);
    assert(has_state(words, STATE_CHECKED));
    call_get_state(desk_session, client, app_bus, rb_path, words);
    assert(!has_state(words, STATE_CHECKED));
  }

  /* Check Text, EditableText, focus/clearFocus, and UTF-8 / character selection. */
  char field_path[96], editor_path[96];
  make_node_path(desk.field, field_path, sizeof(field_path));
  make_node_path(desk.editor, editor_path, sizeof(editor_path));
  {
    char rname[64], desc[64];
    dbus_uint32_t words[2];
    assert(call_get_role(desk_session, client, app_bus, field_path, rname,
                         sizeof(rname)) == 79u);
    assert(strcmp(rname, "entry") == 0);
    call_prop_get_str(desk_session, client, app_bus, field_path,
                      IFACE_ACCESSIBLE, "Description", desc, sizeof(desc));
    assert(strcmp(desc, "Enter account handle") == 0);
    call_get_state(desk_session, client, app_bus, field_path, words);
    assert(has_state(words, STATE_EDITABLE) &&
           has_state(words, STATE_SINGLE_LINE) &&
           has_state(words, STATE_SELECTABLE_TEXT) &&
           has_state(words, STATE_HAS_TOOLTIP) &&
           has_state(words, STATE_FOCUSABLE) && !has_state(words, STATE_FOCUSED));

    assert(call_grab_focus(desk_session, client, app_bus, field_path));
    call_get_state(desk_session, client, app_bus, field_path, words);
    assert(has_state(words, STATE_FOCUSED));

    int clear_idx = find_action_index(desk_session, client, app_bus, field_path,
                                      "clearFocus");
    assert(clear_idx >= 0);
    assert(call_do_action(desk_session, client, app_bus, field_path, clear_idx));
    call_get_state(desk_session, client, app_bus, field_path, words);
    assert(!has_state(words, STATE_FOCUSED));
    assert(call_grab_focus(desk_session, client, app_bus, field_path));

    assert(call_get_role(desk_session, client, app_bus, editor_path, rname,
                         sizeof(rname)) == 61u);
    call_get_state(desk_session, client, app_bus, editor_path, words);
    assert(has_state(words, STATE_EDITABLE) &&
           has_state(words, STATE_MULTI_LINE));

    /* AT-SPI2 character indices map to UTF-8 byte offsets in Scuzz. */
    {
      const char *utf8_val = "é😀z";
      DBusMessage *req = dbus_message_new_method_call(
          app_bus, field_path, IFACE_EDITABLE, "SetTextContents");
      DBusMessage *rep;
      dbus_bool_t ok = FALSE;
      assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &utf8_val,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(strcmp(sz_signal_str_get(desk.field_str), "é😀z") == 0);
      assert(call_prop_get_i32(desk_session, client, app_bus, field_path,
                               IFACE_TEXT, "CharacterCount") == 3);
      assert(call_prop_get_i32(desk_session, client, app_bus, field_path,
                               IFACE_TEXT, "CaretOffset") == 3);
    }

    {
      dbus_int32_t s = 1, e = 2, off_emoji = 1;
      DBusMessage *req =
          dbus_message_new_method_call(app_bus, field_path, IFACE_TEXT, "GetText");
      DBusMessage *rep;
      const char *got = NULL;
      dbus_int32_t cp = 0;
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32,
                                      &e, DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_STRING, &got,
                                   DBUS_TYPE_INVALID));
      assert(strcmp(got, "😀") == 0);
      dbus_message_unref(rep);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_TEXT,
                                         "GetCharacterAtOffset");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &off_emoji,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_INT32, &cp,
                                   DBUS_TYPE_INVALID));
      assert(cp == 0x1f600);
      dbus_message_unref(rep);
    }

    {
      dbus_int32_t sel_num = 0, s = 1, e = 2, gs = 0, ge = 0, nsel = 0;
      DBusMessage *req = dbus_message_new_method_call(
          app_bus, field_path, IFACE_TEXT, "SetSelection");
      DBusMessage *rep;
      dbus_bool_t ok = FALSE;
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &sel_num,
                                      DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32, &e,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(sz_view_text_field_sel_start(desk.field) == 2);
      assert(sz_view_text_field_sel_end(desk.field) == 6);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_TEXT,
                                         "GetNSelections");
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_INT32, &nsel,
                                   DBUS_TYPE_INVALID) &&
             nsel == 1);
      dbus_message_unref(rep);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_TEXT,
                                         "GetSelection");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &sel_num,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_INT32, &gs,
                                   DBUS_TYPE_INT32, &ge, DBUS_TYPE_INVALID));
      assert(gs == 1 && ge == 2);
      dbus_message_unref(rep);
    }

    {
      dbus_int32_t pos = 1, len = 1, del_s = 1, del_e = 2, caret = 2;
      const char *rocket = "🚀";
      DBusMessage *req = dbus_message_new_method_call(
          app_bus, field_path, IFACE_EDITABLE, "InsertText");
      DBusMessage *rep;
      dbus_bool_t ok = FALSE;
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &pos,
                                      DBUS_TYPE_STRING, &rocket,
                                      DBUS_TYPE_INT32, &len, DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(strcmp(sz_signal_str_get(desk.field_str), "é🚀😀z") == 0);
      assert(sz_view_text_field_sel_start(desk.field) == 6 &&
             sz_view_text_field_sel_end(desk.field) == 6);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_EDITABLE,
                                         "DeleteText");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &del_s,
                                      DBUS_TYPE_INT32, &del_e,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(strcmp(sz_signal_str_get(desk.field_str), "é😀z") == 0);
      assert(sz_view_text_field_sel_start(desk.field) == 2 &&
             sz_view_text_field_sel_end(desk.field) == 2);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_TEXT,
                                         "SetCaretOffset");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &caret,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(sz_view_text_field_sel_start(desk.field) == 6 &&
             sz_view_text_field_sel_end(desk.field) == 6);
    }

    {
      dbus_int32_t cs = 1, ce = 2, ppos = 3;
      const char *ed_val = "first\nsecond\nü";
      DBusMessage *req = dbus_message_new_method_call(
          app_bus, field_path, IFACE_EDITABLE, "CopyText");
      DBusMessage *rep;
      dbus_bool_t ok = FALSE;
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &cs, DBUS_TYPE_INT32,
                                      &ce, DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
      dbus_message_unref(rep);

      req = dbus_message_new_method_call(app_bus, field_path, IFACE_EDITABLE,
                                         "PasteText");
      assert(dbus_message_append_args(req, DBUS_TYPE_INT32, &ppos,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(strcmp(sz_signal_str_get(desk.field_str), "é😀z😀") == 0);

      req = dbus_message_new_method_call(app_bus, editor_path, IFACE_EDITABLE,
                                         "SetTextContents");
      assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &ed_val,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             ok);
      dbus_message_unref(rep);
      assert(strcmp(sz_signal_str_get(desk.editor_str), ed_val) == 0);
    }
  }

  /* Check Value interface: stepped Slider, 64-bit Slider, and read-only Progress. */
  char slider_path[96], big_slider_path[96], prog_path[96];
  make_node_path(desk.slider, slider_path, sizeof(slider_path));
  make_node_path(desk.big_slider, big_slider_path, sizeof(big_slider_path));
  make_node_path(desk.progress, prog_path, sizeof(prog_path));
  {
    char vtxt[64] = "";
    assert(call_prop_get_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "MinimumValue") == 0.0);
    assert(call_prop_get_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "MaximumValue") == 100.0);
    assert(call_prop_get_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "MinimumIncrement") == 5.0);
    assert(call_prop_get_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "CurrentValue") == 20.0);
    assert(call_prop_set_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "CurrentValue", 37.0));
    assert(sz_signal_int_get(desk.slider_sig) == 35);
    assert(call_prop_get_f64(desk_session, client, app_bus, slider_path,
                             IFACE_VALUE, "CurrentValue") == 35.0);
    call_prop_get_str(desk_session, client, app_bus, slider_path, IFACE_VALUE,
                      "Text", vtxt, sizeof(vtxt));
    assert(strcmp(vtxt, "35") == 0);

    int inc_idx = find_action_index(desk_session, client, app_bus, slider_path,
                                    "increment");
    int dec_idx = find_action_index(desk_session, client, app_bus, slider_path,
                                    "decrement");
    assert(inc_idx >= 0 && dec_idx >= 0);
    assert(call_do_action(desk_session, client, app_bus, slider_path, inc_idx));
    assert(sz_signal_int_get(desk.slider_sig) == 40);
    assert(call_do_action(desk_session, client, app_bus, slider_path, inc_idx));
    assert(sz_signal_int_get(desk.slider_sig) == 45);
    assert(call_do_action(desk_session, client, app_bus, slider_path, dec_idx));
    assert(sz_signal_int_get(desk.slider_sig) == 40);

    assert(call_prop_set_f64(desk_session, client, app_bus, big_slider_path,
                             IFACE_VALUE, "CurrentValue", 7000000000000.0));
    assert(sz_signal_int_get(desk.big_slider_sig) == 7000000000000LL);

    /* Progress exposes Value read-only and rejects CurrentValue writes. */
    assert(call_prop_get_f64(desk_session, client, app_bus, prog_path,
                             IFACE_VALUE, "CurrentValue") == 45.0);
    assert(!call_prop_set_f64(desk_session, client, app_bus, prog_path,
                              IFACE_VALUE, "CurrentValue", 80.0));
    assert(sz_signal_int_get(desk.progress_sig) == 45);
  }

  /* Check modal View.overlay exclusion: background controls disappear and reject actions/edits. */
  {
    char open_modal_path[96], overlay_path[96], confirm_path[96], close_path[96];
    char same0_path[96], rname[64];
    dbus_uint32_t words[2];
    make_node_path(desk.open_modal_btn, open_modal_path,
                   sizeof(open_modal_path));
    make_node_path(desk.overlay, overlay_path, sizeof(overlay_path));
    make_node_path(desk.modal_confirm_btn, confirm_path, sizeof(confirm_path));
    make_node_path(desk.modal_close_btn, close_path, sizeof(close_path));
    make_node_path(desk.same_btns[0], same0_path, sizeof(same0_path));

    assert(call_do_action(desk_session, client, app_bus, open_modal_path, 0));
    assert(sz_signal_int_get(desk.modal_open_sig) == 1);

    assert(call_prop_get_i32(desk_session, client, app_bus, ROOT_PATH,
                             IFACE_ACCESSIBLE, "ChildCount") == 1);
    assert(call_get_role(desk_session, client, app_bus, overlay_path, rname,
                         sizeof(rname)) == 16u);
    assert(strcmp(rname, "dialog") == 0);
    call_get_state(desk_session, client, app_bus, overlay_path, words);
    assert(has_state(words, STATE_MODAL) && has_state(words, STATE_ACTIVE));

    /* Stale/excluded background node paths report DEFUNCT and reject every mutation. */
    call_get_state(desk_session, client, app_bus, same0_path, words);
    assert(has_state(words, STATE_DEFUNCT));
    assert(!call_do_action(desk_session, client, app_bus, same0_path, 0));
    assert(!call_grab_focus(desk_session, client, app_bus, field_path));
    assert(!call_prop_set_f64(desk_session, client, app_bus, slider_path,
                              IFACE_VALUE, "CurrentValue", 95.0));
    {
      const char *hijack = "blocked";
      DBusMessage *req = dbus_message_new_method_call(
          app_bus, field_path, IFACE_EDITABLE, "SetTextContents");
      DBusMessage *rep;
      dbus_bool_t ok = TRUE;
      assert(dbus_message_append_args(req, DBUS_TYPE_STRING, &hijack,
                                      DBUS_TYPE_INVALID));
      rep = atspi_call(desk_session, client, req);
      dbus_message_unref(req);
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_BOOLEAN, &ok,
                                   DBUS_TYPE_INVALID) &&
             !ok);
      dbus_message_unref(rep);
    }
    assert(desk.same_hits[0] == 1);
    assert(sz_signal_int_get(desk.slider_sig) == 40);
    assert(strcmp(sz_signal_str_get(desk.field_str), "é😀z😀") == 0);

    /* Modal children remain active and close the modal cleanly. */
    assert(call_do_action(desk_session, client, app_bus, confirm_path, 0));
    assert(sz_signal_int_get(desk.modal_hits_sig) == 1);
    assert(call_do_action(desk_session, client, app_bus, close_path, 0));
    assert(sz_signal_int_get(desk.modal_open_sig) == 0);
    assert(call_prop_get_i32(desk_session, client, app_bus, ROOT_PATH,
                             IFACE_ACCESSIBLE, "ChildCount") > SAME_COUNT);
  }

  /* Check stale node ID rejection after View replacement. */
  {
    char old_path[96], trigger_path[96], new_path[96];
    dbus_uint32_t words[2];
    uintptr_t old_id = sz_view_id(desk.ephemeral_btn);
    make_node_path(desk.ephemeral_btn, old_path, sizeof(old_path));
    make_node_path(desk.replace_trigger_btn, trigger_path,
                   sizeof(trigger_path));

    assert(call_do_action(desk_session, client, app_bus, old_path, 0));
    assert(sz_signal_int_get(desk.ephemeral_hits_sig) == 1);

    assert(call_do_action(desk_session, client, app_bus, trigger_path, 0));
    assert(desk.replacement_btn != NULL);
    assert(sz_view_id(desk.replacement_btn) != old_id);
    make_node_path(desk.replacement_btn, new_path, sizeof(new_path));

    call_get_state(desk_session, client, app_bus, old_path, words);
    assert(has_state(words, STATE_DEFUNCT));
    assert(!call_do_action(desk_session, client, app_bus, old_path, 0));
    assert(!call_grab_focus(desk_session, client, app_bus, old_path));
    assert(sz_signal_int_get(desk.ephemeral_hits_sig) == 1);

    assert(call_do_action(desk_session, client, app_bus, new_path, 0));
    assert(sz_signal_int_get(desk.replacement_hits_sig) == 1);
  }

  (void)sz_ui_quiesce(desk_session);
  char *desk_json = dump_json_str(desk.root);
  int desk_sel_start = sz_view_text_field_sel_start(desk.field);
  int desk_sel_end = sz_view_text_field_sel_end(desk.field);
  SzA11ySnapshot *desk_snap = sz_ui_session_a11y_snapshot(desk_session);

  dbus_connection_close(client);
  dbus_connection_unref(client);
  sz_ui_unmount(desk_session);

  /* Verify the recorded journal holds accessibility ops and replays identically on Headless. */
  char *journal = read_all_file(record_path);
  assert(strstr(journal, "\"op\":\"accessibility\"") != NULL);
  assert(strstr(journal, "\"action\":\"activate\"") != NULL);
  assert(strstr(journal, "\"action\":\"setText\"") != NULL);
  assert(strstr(journal, "\"action\":\"setSelection\"") != NULL);
  assert(strstr(journal, "\"action\":\"setValue\"") != NULL);

  Fixture head;
  fixture_init(&head);
  SzUiConfig head_cfg = {.kind = SZ_UI_RUNTIME_HEADLESS,
                         .width = 960,
                         .height = 1280,
                         .scale = 1.0,
                         .title = "AT-SPI2 Proof"};
  SzUiSession *head_session = sz_ui_mount(&head_cfg, head.root);
  assert(head_session);
  sz_ui_session_take_root(head_session);
  assert(sz_ui_pump_sync(head_session));
  sz_ui_script_run_file(head_session, record_path);
  (void)sz_ui_quiesce(head_session);

  assert(strcmp(sz_signal_str_get(head.field_str),
                sz_signal_str_get(desk.field_str)) == 0);
  assert(strcmp(sz_signal_str_get(head.editor_str),
                sz_signal_str_get(desk.editor_str)) == 0);
  assert(sz_signal_int_get(head.check_sig) ==
         sz_signal_int_get(desk.check_sig));
  assert(sz_signal_int_get(head.switch_sig) ==
         sz_signal_int_get(desk.switch_sig));
  assert(sz_signal_int_get(head.radio_sig) ==
         sz_signal_int_get(desk.radio_sig));
  assert(sz_signal_int_get(head.slider_sig) ==
         sz_signal_int_get(desk.slider_sig));
  assert(sz_signal_int_get(head.big_slider_sig) ==
         sz_signal_int_get(desk.big_slider_sig));
  assert(sz_signal_int_get(head.progress_sig) ==
         sz_signal_int_get(desk.progress_sig));
  assert(sz_signal_int_get(head.modal_open_sig) ==
         sz_signal_int_get(desk.modal_open_sig));
  assert(sz_signal_int_get(head.modal_hits_sig) ==
         sz_signal_int_get(desk.modal_hits_sig));
  assert(sz_signal_int_get(head.ephemeral_hits_sig) ==
         sz_signal_int_get(desk.ephemeral_hits_sig));
  assert(sz_signal_int_get(head.replacement_hits_sig) ==
         sz_signal_int_get(desk.replacement_hits_sig));
  assert(sz_signal_int_get(head.same_total_sig) ==
         sz_signal_int_get(desk.same_total_sig));
  assert(sz_signal_int_get(head.same_last_sig) ==
         sz_signal_int_get(desk.same_last_sig));
  for (int i = 0; i < SAME_COUNT; i++)
    assert(head.same_hits[i] == desk.same_hits[i]);
  assert(sz_view_text_field_sel_start(head.field) == desk_sel_start);
  assert(sz_view_text_field_sel_end(head.field) == desk_sel_end);

  char *head_json = dump_json_str(head.root);
  assert(strcmp(head_json, desk_json) == 0);

  SzA11ySnapshot *head_snap = sz_ui_session_a11y_snapshot(head_session);
  assert(sz_a11y_snapshot_count(head_snap) ==
         sz_a11y_snapshot_count(desk_snap));
  for (size_t i = 0; i < sz_a11y_snapshot_count(head_snap); i++) {
    const SzA11yNode *dn = sz_a11y_snapshot_node(desk_snap, i);
    const SzA11yNode *hn = sz_a11y_snapshot_node(head_snap, i);
    assert(dn->role == hn->role);
    assert(dn->bounds.x == hn->bounds.x && dn->bounds.y == hn->bounds.y &&
           dn->bounds.w == hn->bounds.w && dn->bounds.h == hn->bounds.h);
    assert(strcmp(dn->label, hn->label) == 0);
    assert(strcmp(dn->value, hn->value) == 0);
    assert(strcmp(dn->description, hn->description) == 0);
    assert(dn->actions == hn->actions);
    assert(dn->focused == hn->focused);
    assert(dn->editable == hn->editable && dn->multiline == hn->multiline);
    assert(dn->checked == hn->checked && dn->selected == hn->selected &&
           dn->expanded == hn->expanded);
    assert(dn->selection_start == hn->selection_start &&
           dn->selection_end == hn->selection_end);
    assert(dn->number == hn->number && dn->minimum == hn->minimum &&
           dn->maximum == hn->maximum && dn->step == hn->step);
  }

  sz_a11y_snapshot_free(head_snap);
  sz_a11y_snapshot_free(desk_snap);
  free(head_json);
  free(desk_json);
  free(journal);
  sz_ui_unmount(head_session);
  fixture_free_signals(&head);
  fixture_free_signals(&desk);

  /* Test AT_SPI_BUS_ADDRESS private-bus lookup without DISPLAY. */
  daemon_cmd(socks[0], 'd');
  unsetenv("DISPLAY");
  unsetenv("AT_SPI_BUS_ADDRESS");
  assert(sz_embedder_available() == 0);
  assert(sz_embedder_accessibility_available() == 0);
  sz_embedder_shutdown();

  assert(setenv("AT_SPI_BUS_ADDRESS", getenv("DBUS_SESSION_BUS_ADDRESS"), 1) ==
         0);
  {
    SzSignalInt *priv_hits = sz_signal_int(0);
    SzView *priv_root = sz_view_column();
    SzView *priv_btn = sz_view_button("PrivateBus", on_inc_sig, priv_hits);
    char priv_btn_path[96], rname[64];
    sz_view_add_child(priv_root, priv_btn);
    make_node_path(priv_btn, priv_btn_path, sizeof(priv_btn_path));

    SzUiConfig pcfg = {.kind = SZ_UI_RUNTIME_DESKTOP,
                       .width = 320,
                       .height = 200,
                       .scale = 1.0,
                       .title = "Private AT-SPI2"};
    SzUiSession *psess = sz_ui_mount(&pcfg, priv_root);
    assert(psess);
    sz_ui_session_take_root(psess);
    assert(sz_ui_pump_sync(psess));
    assert(sz_embedder_accessibility_available() == 1);

    DBusConnection *pclient =
        dbus_connection_open_private(getenv("AT_SPI_BUS_ADDRESS"), NULL);
    assert(pclient);
    dbus_connection_set_exit_on_disconnect(pclient, FALSE);
    assert(dbus_bus_register(pclient, NULL));

    char pbus[128] = "", proot[128] = "", pru[128] = "";
    DBusMessage *req = dbus_message_new_method_call(
        REGISTRY_SERVICE, ROOT_PATH, TEST_IFACE, "GetEmbedded");
    DBusMessage *rep =
        dbus_connection_send_with_reply_and_block(pclient, req, 1000, NULL);
    dbus_message_unref(req);
    assert(rep);
    {
      const char *eb = NULL, *ep = NULL, *ru = NULL;
      assert(dbus_message_get_args(rep, NULL, DBUS_TYPE_STRING, &eb,
                                   DBUS_TYPE_STRING, &ep, DBUS_TYPE_STRING, &ru,
                                   DBUS_TYPE_INVALID));
      snprintf(pbus, sizeof(pbus), "%s", eb);
      snprintf(proot, sizeof(proot), "%s", ep);
      snprintf(pru, sizeof(pru), "%s", ru);
    }
    dbus_message_unref(rep);
    assert(pbus[0] != '\0' && strcmp(proot, ROOT_PATH) == 0);
    assert(call_get_role(psess, pclient, pbus, priv_btn_path, rname,
                         sizeof(rname)) == 43u);
    assert(call_do_action(psess, pclient, pbus, priv_btn_path, 0));
    assert(sz_signal_int_get(priv_hits) == 1);

    dbus_connection_close(pclient);
    dbus_connection_unref(pclient);
    sz_ui_unmount(psess);
    sz_signal_int_free(priv_hits);
  }
  unsetenv("AT_SPI_BUS_ADDRESS");

  assert(write(socks[0], "q", 1) == 1);
  close(socks[0]);
  assert(waitpid(daemon_pid, &status, 0) == daemon_pid && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);

  unlink(record_path);
  rmdir(tmp_dir);
  puts("Linux Desktop AT-SPI2 accessibility and Headless replay parity tests pass.");
  return 0;
}
