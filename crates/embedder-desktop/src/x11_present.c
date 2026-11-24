#define _POSIX_C_SOURCE 200809L

#include "scuzz_embedder.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#if defined(__has_include)
#if __has_include(<X11/Xim.h>)
#include <X11/Xim.h>
#define SCUZZ_HAVE_XIM 1
#endif
#endif
#include <dbus/dbus.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EVENT_CAP 256
#define TEXT_RING 64
#define TEXT_LEN 128
#define KEY_NAME_LEN 32

static Display *g_dpy;
static Window g_win;
static GC g_gc;
static XImage *g_img;
static char *g_img_data;
static Atom g_wm_delete;
static int g_w;
static int g_h;
static int g_win_w; /* live window size from ConfigureNotify */
static int g_win_h;
static int g_ready;
static int g_user_quit;
static int g_probed;
static int g_probe_ok;

static SzInputEvent g_queue[EVENT_CAP];
static char *g_a11y_text_q[EVENT_CAP];
static char *g_a11y_poll_text;
static char *g_win_title;
static int g_q_head;
static int g_q_tail;
static char g_key_bufs[TEXT_RING][KEY_NAME_LEN];
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;
static int g_key_repeat_pending;
static unsigned int g_held_keycode;
static int g_key_held;
static char *g_clip;
static int g_clip_own;
static Atom g_atom_clipboard;
static Atom g_atom_utf8;
static Atom g_atom_targets;
static Atom g_atom_prop;
#if SCUZZ_HAVE_XIM
static XIM g_xim;
static XIC g_xic;
#endif

/* Handle one X event. Returns nonzero when the window session ended
 * (destroy or WM close); the caller must stop using g_dpy/g_win.
 * Defined below; the clipboard wait needs it before its definition. */
static int x11_dispatch_event(XEvent *ev);
static void x11_drain_pending(void);

/* X errors are async: the default handler exits the process. A clipboard
 * requestor can die between XChangeProperty and delivery; a stale window
 * id must not take the app down. Log and continue. */
static int x11_on_error(Display *dpy, XErrorEvent *err) {
  char msg[128];
  if (!dpy || !err)
    return 0;
  XGetErrorText(dpy, err->error_code, msg, sizeof msg);
  fprintf(stderr, "scuzz embedder: X error %d (%s) op %d.%d\n",
          (int)err->error_code, msg, (int)err->request_code,
          (int)err->minor_code);
  return 0;
}

static char *clip_dup(const char *s) {
  size_t n;
  char *out;
  if (!s)
    return NULL;
  n = strlen(s);
  out = (char *)malloc(n + 1);
  if (!out)
    return NULL;
  memcpy(out, s, n + 1);
  return out;
}

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS "org.freedesktop.portal.Settings"
#define PORTAL_NAMESPACE "org.freedesktop.appearance"

static DBusConnection *g_motion_bus;
static char *g_motion_owner;
static dbus_uint32_t g_motion_serial;
static int g_motion_started;
static int g_motion_reduced;

static void motion_owner(const char *owner) {
  free(g_motion_owner);
  g_motion_owner = owner && *owner ? clip_dup(owner) : NULL;
  g_motion_serial = 0;
}

static int motion_value(DBusMessage *message, int *value) {
  DBusMessageIter outer, inner;
  dbus_uint32_t raw;
  if (!dbus_message_iter_init(message, &outer) ||
      dbus_message_iter_get_arg_type(&outer) != DBUS_TYPE_VARIANT)
    return 0;
  dbus_message_iter_recurse(&outer, &inner);
  if (dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_UINT32 ||
      dbus_message_iter_next(&outer))
    return 0;
  dbus_message_iter_get_basic(&inner, &raw);
  *value = raw == 1;
  return 1;
}

static DBusMessage *motion_request(void) {
  const char *space = PORTAL_NAMESPACE, *key = "reduced-motion";
  DBusMessage *message;
  if (!g_motion_owner)
    return NULL;
  message = dbus_message_new_method_call(g_motion_owner, PORTAL_PATH,
                                         PORTAL_SETTINGS, "ReadOne");
  if (!message)
    return NULL;
  dbus_message_set_auto_start(message, FALSE);
  if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &space,
                                DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID)) {
    dbus_message_unref(message);
    return NULL;
  }
  return message;
}

static void motion_read_later(void) {
  DBusMessage *message = motion_request();
  if (message) {
    (void)dbus_connection_send(g_motion_bus, message, &g_motion_serial);
    dbus_message_unref(message);
  }
}

static void motion_shutdown(void) {
  if (g_motion_bus) {
    dbus_connection_close(g_motion_bus);
    dbus_connection_unref(g_motion_bus);
  }
  g_motion_bus = NULL;
  motion_owner(NULL);
  g_motion_started = 0;
  g_motion_reduced = 0;
}

int sz_embedder_reduced_motion(void) {
  DBusMessage *message, *reply;
  const char *name = PORTAL_NAME, *owner;
  const char *address = getenv("DBUS_SESSION_BUS_ADDRESS");
  if (g_motion_started)
    return g_motion_reduced;
  g_motion_started = 1;
  /* Do not start a session bus when the desktop has none. */
  if (!address || !*address)
    return 0;
  g_motion_bus = dbus_bus_get_private(DBUS_BUS_SESSION, NULL);
  if (!g_motion_bus)
    return 0;
  dbus_connection_set_exit_on_disconnect(g_motion_bus, FALSE);
  dbus_bus_add_match(g_motion_bus,
      "type='signal',sender='org.freedesktop.DBus',"
      "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
      "path='/org/freedesktop/DBus',arg0='" PORTAL_NAME "'", NULL);
  dbus_bus_add_match(g_motion_bus,
      "type='signal',sender='" PORTAL_NAME "',interface='" PORTAL_SETTINGS
      "',member='SettingChanged',path='" PORTAL_PATH "'", NULL);
  message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                         DBUS_INTERFACE_DBUS, "GetNameOwner");
  if (!message)
    return 0;
  dbus_message_append_args(message, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
  reply = dbus_connection_send_with_reply_and_block(g_motion_bus, message, 100, NULL);
  dbus_message_unref(message);
  if (reply) {
    if (dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID))
      motion_owner(owner);
    dbus_message_unref(reply);
  }
  message = motion_request();
  if (!message)
    return 0;
  reply = dbus_connection_send_with_reply_and_block(g_motion_bus, message, 100, NULL);
  dbus_message_unref(message);
  if (reply) {
    (void)motion_value(reply, &g_motion_reduced);
    dbus_message_unref(reply);
  }
  return g_motion_reduced;
}

static void motion_poll(void) {
  DBusMessage *message;
  if (!g_motion_bus || !dbus_connection_get_is_connected(g_motion_bus))
    return;
  dbus_connection_read_write(g_motion_bus, 0);
  for (int i = 0; i < 64 && (message = dbus_connection_pop_message(g_motion_bus)); i++) {
    const char *sender = dbus_message_get_sender(message);
    if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged") &&
        sender && !strcmp(sender, DBUS_SERVICE_DBUS) &&
        dbus_message_has_path(message, DBUS_PATH_DBUS)) {
      const char *name, *old_owner, *new_owner;
      if (dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &name,
          DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID) &&
          !strcmp(name, PORTAL_NAME)) {
        motion_owner(new_owner);
        motion_read_later();
      }
    } else if (g_motion_owner && sender && !strcmp(sender, g_motion_owner)) {
      if (g_motion_serial && dbus_message_get_reply_serial(message) == g_motion_serial) {
        (void)motion_value(message, &g_motion_reduced);
        g_motion_serial = 0;
      } else if (dbus_message_is_signal(message, PORTAL_SETTINGS, "SettingChanged") &&
                 dbus_message_has_path(message, PORTAL_PATH)) {
        DBusMessageIter iter, value;
        const char *space, *key;
        dbus_uint32_t raw;
        if (dbus_message_iter_init(message, &iter) &&
            dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
          dbus_message_iter_get_basic(&iter, &space);
          if (dbus_message_iter_next(&iter) &&
              dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&iter, &key);
            if (!strcmp(space, PORTAL_NAMESPACE) && !strcmp(key, "reduced-motion") &&
                dbus_message_iter_next(&iter) &&
                dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
              dbus_message_iter_recurse(&iter, &value);
              if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_UINT32 &&
                  !dbus_message_iter_next(&iter)) {
                dbus_message_iter_get_basic(&value, &raw);
                g_motion_reduced = raw == 1;
                /* A change signal supersedes an older read reply. */
                g_motion_serial = 0;
              }
            }
          }
        }
      }
    }
    dbus_message_unref(message);
  }
}

static void x11_clip_atoms(void) {
  if (!g_dpy || g_atom_clipboard)
    return;
  g_atom_clipboard = XInternAtom(g_dpy, "CLIPBOARD", False);
  g_atom_utf8 = XInternAtom(g_dpy, "UTF8_STRING", False);
  g_atom_targets = XInternAtom(g_dpy, "TARGETS", False);
  g_atom_prop = XInternAtom(g_dpy, "SCUZZ_CLIPBOARD", False);
}

static void x11_serve_selection(XSelectionRequestEvent *req) {
  XEvent notify;
  Atom prop;
  int ok = 0;
  if (!req || !g_dpy)
    return;
  x11_clip_atoms();
  /* ICCCM: obsolete clients send None; answer on the target property. */
  prop = req->property == None ? req->target : req->property;
  if (req->target == g_atom_targets) {
    Atom targets[3];
    targets[0] = g_atom_targets;
    targets[1] = g_atom_utf8;
    targets[2] = XA_STRING;
    XChangeProperty(g_dpy, req->requestor, prop, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)targets, 3);
    ok = 1;
  } else if ((req->target == g_atom_utf8 || req->target == XA_STRING) &&
             g_clip) {
    XChangeProperty(g_dpy, req->requestor, prop, req->target, 8,
                    PropModeReplace, (unsigned char *)g_clip,
                    (int)strlen(g_clip));
    ok = 1;
  }
  memset(&notify, 0, sizeof notify);
  notify.xselection.type = SelectionNotify;
  notify.xselection.display = req->display;
  notify.xselection.requestor = req->requestor;
  notify.xselection.selection = req->selection;
  notify.xselection.target = req->target;
  notify.xselection.property = ok ? prop : None;
  notify.xselection.time = req->time;
  XSendEvent(g_dpy, req->requestor, True, NoEventMask, &notify);
}

int sz_embedder_clipboard_set(const char *text) {
  free(g_clip);
  g_clip = clip_dup(text ? text : "");
  if (!g_clip)
    return 0;
  if (g_dpy && g_win) {
    x11_clip_atoms();
    XStoreBytes(g_dpy, g_clip, (int)strlen(g_clip));
    XSetSelectionOwner(g_dpy, g_atom_clipboard, g_win, CurrentTime);
    g_clip_own = XGetSelectionOwner(g_dpy, g_atom_clipboard) == g_win;
    XFlush(g_dpy);
  }
  return 1;
}

/* Read a selection property. Large selections do not fit one request:
 * read in chunks until the owner has no more bytes, then delete the
 * property (ICCCM leaves the delete to the requestor). */
static char *x11_read_prop(Atom prop) {
  long offset = 0;
  size_t len = 0;
  size_t cap = 0;
  char *out = NULL;
  if (!g_dpy || !g_win || prop == None)
    return NULL;
  for (;;) {
    Atom type = None;
    int fmt = 0;
    unsigned long nitems = 0;
    unsigned long after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(g_dpy, g_win, prop, offset, 16384, False,
                           AnyPropertyType, &type, &fmt, &nitems, &after,
                           &data) != Success) {
      free(out);
      return NULL;
    }
    if (fmt != 8) {
      /* UTF8_STRING / STRING are 8-bit. Do not guess at other formats. */
      if (data)
        XFree(data);
      free(out);
      return NULL;
    }
    if (nitems > 0 && data) {
      if (len + nitems + 1 > cap) {
        size_t ncap = cap ? cap : 4096;
        char *nb;
        while (ncap < len + nitems + 1) {
          if (ncap > SIZE_MAX / 2) {
            XFree(data);
            free(out);
            return NULL;
          }
          ncap *= 2;
        }
        nb = (char *)realloc(out, ncap);
        if (!nb) {
          XFree(data);
          free(out);
          return NULL;
        }
        out = nb;
        cap = ncap;
      }
      memcpy(out + len, data, nitems);
      len += nitems;
    }
    if (data)
      XFree(data);
    if (after == 0)
      break;
    offset += (long)((nitems + 3) / 4);
  }
  XDeleteProperty(g_dpy, g_win, prop);
  if (!out) {
    out = (char *)malloc(1);
    if (out)
      out[0] = '\0';
    return out;
  }
  out[len] = '\0';
  return out;
}

char *sz_embedder_clipboard_get(void) {
  int i;
  if (g_clip_own && g_clip)
    return clip_dup(g_clip);
  if (!g_dpy || !g_win)
    return g_clip ? clip_dup(g_clip) : NULL;
  x11_clip_atoms();
  XConvertSelection(g_dpy, g_atom_clipboard, g_atom_utf8, g_atom_prop, g_win,
                    CurrentTime);
  XFlush(g_dpy);
  for (i = 0; i < 50; i++) {
    while (XPending(g_dpy)) {
      XEvent ev;
      XNextEvent(g_dpy, &ev);
      if (ev.type == SelectionNotify) {
        char *got = NULL;
        if (ev.xselection.property != None)
          got = x11_read_prop(ev.xselection.property);
        if (got)
          return got;
        return g_clip ? clip_dup(g_clip) : NULL;
      }
      /* Dispatch every other event the same way present() does. A paste
       * wait must not eat key presses, clicks, or close requests. */
      if (x11_dispatch_event(&ev))
        return NULL; /* shutdown ran; g_clip is freed */
    }
    {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 2000000L; /* 2 ms */
      nanosleep(&ts, NULL);
    }
  }
  return g_clip ? clip_dup(g_clip) : NULL;
}

int sz_embedder_available(void) {
  if (g_dpy)
    return 1;
  if (g_user_quit)
    return 0;
  if (g_probed)
    return g_probe_ok;
  g_probed = 1;
  g_probe_ok = 0;
  {
    const char *d = getenv("DISPLAY");
    Display *dpy;
    if (!d || !*d)
      return 0;
    dpy = XOpenDisplay(NULL);
    if (!dpy)
      return 0;
    XCloseDisplay(dpy);
    g_probe_ok = 1;
    return 1;
  }
}

double sz_embedder_display_scale(void) {
  return 1.0;
}

int sz_embedder_alive(void) {
  if (g_user_quit)
    return 0;
  if (g_ready)
    return 1;
  return sz_embedder_available();
}

static int q_full(void) {
  return ((g_q_tail + 1) % EVENT_CAP) == g_q_head;
}

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    const SzInputEvent *ev = &g_queue[i];
    if (ev->kind == SZ_INPUT_KEY && (ev->key == slot || ev->text == slot))
      return 1;
    if ((ev->kind == SZ_INPUT_COMPOSE || ev->kind == SZ_INPUT_TEXT_EDIT) &&
        ev->text == slot)
      return 1;
  }
  return 0;
}

static void stash_key_text(const char *name, const char *text, const char **out_key,
                           const char **out_text) {
  size_t nk;
  size_t nt;
  char *kdst;
  char *tdst;
  if (!name)
    name = "";
  if (!text)
    text = "";
  nk = strlen(name);
  nt = strlen(text);
  if (nk >= KEY_NAME_LEN)
    nk = KEY_NAME_LEN - 1;
  if (nt >= TEXT_LEN)
    nt = TEXT_LEN - 1;
  kdst = g_key_bufs[g_text_i];
  tdst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  memcpy(kdst, name, nk);
  kdst[nk] = '\0';
  memcpy(tdst, text, nt);
  tdst[nt] = '\0';
  *out_key = kdst;
  *out_text = tdst;
}

static int q_push(const SzInputEvent *ev) {
  int next;
  if (!ev)
    return 0;
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head)
    return 0; /* full */
  free(g_a11y_text_q[g_q_tail]);
  g_a11y_text_q[g_q_tail] = NULL;
  g_queue[g_q_tail] = *ev;
  g_q_tail = next;
  return 1;
}

static int q_free_slots(void) {
  int used = (g_q_tail - g_q_head + EVENT_CAP) % EVENT_CAP;
  return (EVENT_CAP - 1) - used;
}

static void q_clear_a11y_text(void) {
  int i;
  for (i = 0; i < EVENT_CAP; i++) {
    free(g_a11y_text_q[i]);
    g_a11y_text_q[i] = NULL;
  }
  free(g_a11y_poll_text);
  g_a11y_poll_text = NULL;
}

/* Weak fallbacks keep standalone embedder tests independent of libscuzz_rt.a. */
__attribute__((weak)) size_t
sz_a11y_snapshot_count(const SzA11ySnapshot *snapshot) {
  (void)snapshot;
  return 0;
}

__attribute__((weak)) const SzA11yNode *
sz_a11y_snapshot_node(const SzA11ySnapshot *snapshot, size_t index) {
  (void)snapshot;
  (void)index;
  return NULL;
}

__attribute__((weak)) void sz_a11y_snapshot_free(SzA11ySnapshot *snapshot) {
  (void)snapshot;
}

#define ATSPI_BUS_SERVICE "org.a11y.Bus"
#define ATSPI_BUS_PATH "/org/a11y/bus"
#define ATSPI_BUS_IFACE "org.a11y.Bus"
#define ATSPI_REGISTRY_SERVICE "org.a11y.atspi.Registry"
#define ATSPI_SOCKET_IFACE "org.a11y.atspi.Socket"
#define ATSPI_ROOT_PATH "/org/a11y/atspi/accessible/root"
#define ATSPI_NODE_PREFIX "/org/a11y/atspi/accessible/"
#define ATSPI_CACHE_PATH "/org/a11y/atspi/cache"
#define ATSPI_NULL_PATH "/org/a11y/atspi/null"

#define ATSPI_IFACE_ACCESSIBLE "org.a11y.atspi.Accessible"
#define ATSPI_IFACE_APPLICATION "org.a11y.atspi.Application"
#define ATSPI_IFACE_ACTION "org.a11y.atspi.Action"
#define ATSPI_IFACE_COMPONENT "org.a11y.atspi.Component"
#define ATSPI_IFACE_VALUE "org.a11y.atspi.Value"
#define ATSPI_IFACE_TEXT "org.a11y.atspi.Text"
#define ATSPI_IFACE_EDITABLE "org.a11y.atspi.EditableText"
#define ATSPI_IFACE_CACHE "org.a11y.atspi.Cache"
#define ATSPI_IFACE_EVENT_OBJECT "org.a11y.atspi.Event.Object"

enum {
  ATSPI_STATE_ACTIVE = 1,
  ATSPI_STATE_CHECKED = 4,
  ATSPI_STATE_COLLAPSED = 5,
  ATSPI_STATE_DEFUNCT = 6,
  ATSPI_STATE_EDITABLE = 7,
  ATSPI_STATE_ENABLED = 8,
  ATSPI_STATE_EXPANDABLE = 9,
  ATSPI_STATE_EXPANDED = 10,
  ATSPI_STATE_FOCUSABLE = 11,
  ATSPI_STATE_FOCUSED = 12,
  ATSPI_STATE_HAS_TOOLTIP = 13,
  ATSPI_STATE_MODAL = 16,
  ATSPI_STATE_MULTI_LINE = 17,
  ATSPI_STATE_SELECTABLE = 22,
  ATSPI_STATE_SELECTED = 23,
  ATSPI_STATE_SENSITIVE = 24,
  ATSPI_STATE_SHOWING = 25,
  ATSPI_STATE_SINGLE_LINE = 26,
  ATSPI_STATE_VISIBLE = 30,
  ATSPI_STATE_SELECTABLE_TEXT = 38,
  ATSPI_STATE_CHECKABLE = 41,
  ATSPI_STATE_HAS_POPUP = 42
};

enum {
  ATSPI_COORD_SCREEN = 0,
  ATSPI_COORD_WINDOW = 1,
  ATSPI_COORD_PARENT = 2
};

enum {
  ATSPI_LAYER_INVALID = 0,
  ATSPI_LAYER_WIDGET = 3,
  ATSPI_LAYER_POPUP = 5,
  ATSPI_LAYER_OVERLAY = 6,
  ATSPI_LAYER_WINDOW = 7
};

static DBusConnection *g_a11y_bus;
static int g_a11y_probed;
static SzA11ySnapshot *g_a11y_snapshot;
static int g_a11y_w;
static int g_a11y_h;
static char *g_a11y_plug_bus;
static char *g_a11y_plug_path;
static dbus_int32_t g_a11y_app_id;

static int32_t a11y_floor_i32(float v) {
  int32_t t;
  if (!(v == v))
    return 0;
  if (v <= -536870912.0f)
    return -536870912;
  if (v >= 536870912.0f)
    return 536870912;
  t = (int32_t)v;
  return ((float)t > v) ? (t - 1) : t;
}

static int32_t a11y_ceil_i32(float v) {
  int32_t t;
  if (!(v == v))
    return 0;
  if (v <= -536870912.0f)
    return -536870912;
  if (v >= 536870912.0f)
    return 536870912;
  t = (int32_t)v;
  return ((float)t < v) ? (t + 1) : t;
}

static int64_t a11y_clamp_double_i64(double d, int64_t lo, int64_t hi) {
  int64_t r;
  if (lo > hi) {
    int64_t tmp = lo;
    lo = hi;
    hi = tmp;
  }
  if (!(d == d))
    return lo;
  if (d <= -9223372036854775808.0)
    return lo;
  if (d >= 9223372036854775808.0)
    return hi;
  if (d >= 0.0)
    r = (d >= 9223372036854774784.0) ? (int64_t)d : (int64_t)(d + 0.5);
  else
    r = (int64_t)(d - 0.5);
  if (r < lo)
    return lo;
  if (r > hi)
    return hi;
  return r;
}

static int utf8_step(const unsigned char *s, size_t len, size_t pos,
                     uint32_t *out_cp) {
  unsigned char c0;
  if (pos >= len)
    return 0;
  c0 = s[pos];
  if (c0 < 0x80) {
    if (out_cp)
      *out_cp = c0;
    return 1;
  }
  if ((c0 & 0xe0) == 0xc0 && pos + 1 < len && (s[pos + 1] & 0xc0) == 0x80) {
    uint32_t cp = ((uint32_t)(c0 & 0x1f) << 6) | (uint32_t)(s[pos + 1] & 0x3f);
    if (cp >= 0x80) {
      if (out_cp)
        *out_cp = cp;
      return 2;
    }
  } else if ((c0 & 0xf0) == 0xe0 && pos + 2 < len &&
             (s[pos + 1] & 0xc0) == 0x80 && (s[pos + 2] & 0xc0) == 0x80) {
    uint32_t cp = ((uint32_t)(c0 & 0x0f) << 12) |
                  ((uint32_t)(s[pos + 1] & 0x3f) << 6) |
                  (uint32_t)(s[pos + 2] & 0x3f);
    if (cp >= 0x800 && (cp < 0xd800 || cp > 0xdfff)) {
      if (out_cp)
        *out_cp = cp;
      return 3;
    }
  } else if ((c0 & 0xf8) == 0xf0 && pos + 3 < len &&
             (s[pos + 1] & 0xc0) == 0x80 && (s[pos + 2] & 0xc0) == 0x80 &&
             (s[pos + 3] & 0xc0) == 0x80) {
    uint32_t cp = ((uint32_t)(c0 & 0x07) << 18) |
                  ((uint32_t)(s[pos + 1] & 0x3f) << 12) |
                  ((uint32_t)(s[pos + 2] & 0x3f) << 6) |
                  (uint32_t)(s[pos + 3] & 0x3f);
    if (cp >= 0x10000 && cp <= 0x10ffff) {
      if (out_cp)
        *out_cp = cp;
      return 4;
    }
  }
  if (out_cp)
    *out_cp = 0xfffdu;
  return 1;
}

static dbus_int32_t utf8_char_count(const char *s) {
  size_t len, pos = 0;
  dbus_int32_t count = 0;
  if (!s)
    return 0;
  len = strlen(s);
  while (pos < len) {
    pos += (size_t)utf8_step((const unsigned char *)s, len, pos, NULL);
    if (count < INT32_MAX)
      count++;
  }
  return count;
}

static size_t utf8_char_to_byte(const char *s, dbus_int32_t char_idx) {
  size_t len, pos = 0;
  dbus_int32_t cur = 0;
  if (!s || char_idx <= 0)
    return 0;
  len = strlen(s);
  while (pos < len && cur < char_idx) {
    pos += (size_t)utf8_step((const unsigned char *)s, len, pos, NULL);
    cur++;
  }
  return pos;
}

static dbus_int32_t utf8_byte_to_char(const char *s, int byte_idx) {
  size_t len, target, pos = 0;
  dbus_int32_t count = 0;
  if (!s || byte_idx <= 0)
    return 0;
  len = strlen(s);
  target = (size_t)byte_idx;
  if (target > len)
    target = len;
  while (pos < target) {
    pos += (size_t)utf8_step((const unsigned char *)s, len, pos, NULL);
    if (count < INT32_MAX)
      count++;
  }
  return count;
}

static dbus_int32_t utf8_char_at(const char *s, dbus_int32_t char_idx) {
  size_t len, pos;
  uint32_t cp = 0;
  if (!s || char_idx < 0)
    return 0;
  len = strlen(s);
  pos = utf8_char_to_byte(s, char_idx);
  if (pos >= len)
    return 0;
  (void)utf8_step((const unsigned char *)s, len, pos, &cp);
  return (dbus_int32_t)cp;
}

static int a11y_enqueue(uintptr_t id, int action, const char *text, int start,
                        int end, int64_t value) {
  SzInputEvent ev;
  char *copy = NULL;
  int next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head)
    return 0;
  if (text) {
    copy = clip_dup(text);
    if (!copy)
      return 0;
  }
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_ACCESSIBILITY;
  ev.a11y_id = id;
  ev.a11y_action = action;
  ev.text = copy;
  ev.a11y_start = start;
  ev.a11y_end = end;
  ev.a11y_value = value;
  free(g_a11y_text_q[g_q_tail]);
  g_a11y_text_q[g_q_tail] = copy;
  g_queue[g_q_tail] = ev;
  g_q_tail = next;
  return 1;
}

static const SzA11yNode *a11y_find_node(const SzA11ySnapshot *snap,
                                        uintptr_t id) {
  size_t i, n;
  if (!snap || !id)
    return NULL;
  n = sz_a11y_snapshot_count(snap);
  for (i = 0; i < n; i++) {
    const SzA11yNode *node = sz_a11y_snapshot_node(snap, i);
    if (node && node->id == id)
      return node;
  }
  return NULL;
}

static const char *a11y_node_text(const SzA11yNode *node) {
  int i;
  const char *latest = NULL;
  if (!node)
    return "";
  if (node->editable) {
    for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
      if (g_queue[i].kind == SZ_INPUT_ACCESSIBILITY &&
          g_queue[i].a11y_id == node->id &&
          g_queue[i].a11y_action == SZ_A11Y_SET_TEXT && g_queue[i].text)
        latest = g_queue[i].text;
    }
    if (latest)
      return latest;
    return node->value ? node->value : "";
  }
  return node->label ? node->label : "";
}

static void a11y_node_selection_bytes(const SzA11yNode *node, int *out_start,
                                      int *out_end) {
  int s = 0, e = 0, i;
  if (node && node->editable) {
    s = node->selection_start;
    e = node->selection_end;
    for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
      if (g_queue[i].kind == SZ_INPUT_ACCESSIBILITY &&
          g_queue[i].a11y_id == node->id) {
        if (g_queue[i].a11y_action == SZ_A11Y_SET_SELECTION) {
          s = g_queue[i].a11y_start;
          e = g_queue[i].a11y_end;
        } else if (g_queue[i].a11y_action == SZ_A11Y_SET_TEXT &&
                   g_queue[i].text) {
          int len = (int)strlen(g_queue[i].text);
          s = len;
          e = len;
        }
      }
    }
  }
  *out_start = s;
  *out_end = e;
}

static void a11y_make_path(uintptr_t id, char *buf, size_t cap) {
  if (id == 0)
    snprintf(buf, cap, "%s", ATSPI_ROOT_PATH);
  else
    snprintf(buf, cap, "%s%llu", ATSPI_NODE_PREFIX, (unsigned long long)id);
}

/* Parse the root path or a node path. Set out_id to 0 for the root. */
static int a11y_parse_path(const char *path, uintptr_t *out_id) {
  const char *p;
  unsigned long long v = 0;
  if (!path || !out_id)
    return 0;
  if (!strcmp(path, ATSPI_ROOT_PATH)) {
    *out_id = 0;
    return 1;
  }
  if (strncmp(path, ATSPI_NODE_PREFIX, sizeof(ATSPI_NODE_PREFIX) - 1) != 0)
    return 0;
  p = path + (sizeof(ATSPI_NODE_PREFIX) - 1);
  if (*p < '1' || *p > '9')
    return 0;
  while (*p) {
    unsigned d;
    if (*p < '0' || *p > '9')
      return 0;
    d = (unsigned)(*p - '0');
    if (v > (ULLONG_MAX - d) / 10ULL)
      return 0;
    v = v * 10ULL + d;
    p++;
  }
  if (v == 0 || v > (unsigned long long)UINTPTR_MAX)
    return 0;
  *out_id = (uintptr_t)v;
  return 1;
}

static int a11y_has_value(const SzA11yNode *node) {
  if (!node)
    return 0;
  if ((node->role == SZ_A11Y_PROGRESS || node->role == SZ_A11Y_CIRCULAR) && node->step < 0)
    return 0;
  return node->role == SZ_A11Y_SLIDER || node->role == SZ_A11Y_SPLIT ||
         node->role == SZ_A11Y_PROGRESS || node->role == SZ_A11Y_CIRCULAR;
}

static int a11y_has_text(const SzA11yNode *node) {
  if (!node)
    return 0;
  if (node->editable || node->role == SZ_A11Y_TEXT ||
      node->role == SZ_A11Y_HEADING || node->role == SZ_A11Y_BADGE)
    return 1;
  return node->label && node->label[0] != '\0';
}

typedef struct {
  int bit;
  const char *name;
} A11yActionSpec;

static const A11yActionSpec k_a11y_actions[] = {
    {SZ_A11Y_ACTIVATE, "activate"},
    {SZ_A11Y_FOCUS, "focus"},
    {SZ_A11Y_CLEAR_FOCUS, "clearFocus"},
    {SZ_A11Y_INCREMENT, "increment"},
    {SZ_A11Y_DECREMENT, "decrement"},
};

static dbus_int32_t a11y_action_count(const SzA11yNode *node) {
  dbus_int32_t n = 0;
  size_t i;
  if (!node)
    return 0;
  for (i = 0; i < sizeof(k_a11y_actions) / sizeof(k_a11y_actions[0]); i++) {
    if (node->actions & k_a11y_actions[i].bit)
      n++;
  }
  return n;
}

static const A11yActionSpec *a11y_action_at(const SzA11yNode *node,
                                            dbus_int32_t idx) {
  dbus_int32_t cur = 0;
  size_t i;
  if (!node || idx < 0)
    return NULL;
  for (i = 0; i < sizeof(k_a11y_actions) / sizeof(k_a11y_actions[0]); i++) {
    if (node->actions & k_a11y_actions[i].bit) {
      if (cur == idx)
        return &k_a11y_actions[i];
      cur++;
    }
  }
  return NULL;
}

static void a11y_role_info(int is_root, const SzA11yNode *node,
                           dbus_uint32_t *out_role, const char **out_name) {
  dbus_uint32_t r = 0;
  const char *name = "invalid";
  if (is_root) {
    *out_role = 75u;
    *out_name = "application";
    return;
  }
  if (!node) {
    *out_role = 0u;
    *out_name = "invalid";
    return;
  }
  switch (node->role) {
  case SZ_A11Y_BUTTON:
  case SZ_A11Y_ICON_BUTTON:
  case SZ_A11Y_FAB:
  case SZ_A11Y_OUTLINED:
  case SZ_A11Y_TEXT_BUTTON:
  case SZ_A11Y_ACTION_CHIP:
  case SZ_A11Y_INK_WELL:
  case SZ_A11Y_EXPANSION:
    r = 43u;
    name = "push button";
    break;
  case SZ_A11Y_CHECKBOX:
  case SZ_A11Y_CHECK_TILE:
    r = 7u;
    name = "check box";
    break;
  case SZ_A11Y_SWITCH:
  case SZ_A11Y_SWITCH_TILE:
  case SZ_A11Y_CHIP:
  case SZ_A11Y_FILTER_CHIP:
  case SZ_A11Y_INPUT_CHIP:
    r = 62u;
    name = "toggle button";
    break;
  case SZ_A11Y_RADIO:
  case SZ_A11Y_CHOICE_CHIP:
  case SZ_A11Y_RADIO_TILE:
    r = 44u;
    name = "radio button";
    break;
  case SZ_A11Y_TEXT_FIELD:
    r = 79u;
    name = "entry";
    break;
  case SZ_A11Y_EDITOR:
    r = 61u;
    name = "text";
    break;
  case SZ_A11Y_TEXT:
  case SZ_A11Y_BADGE:
    r = 29u;
    name = "label";
    break;
  case SZ_A11Y_HEADING:
    r = 83u;
    name = "heading";
    break;
  case SZ_A11Y_IMAGE:
  case SZ_A11Y_AVATAR:
    r = 27u;
    name = "image";
    break;
  case SZ_A11Y_ICON:
    r = 26u;
    name = "icon";
    break;
  case SZ_A11Y_SLIDER:
    r = 51u;
    name = "slider";
    break;
  case SZ_A11Y_SPLIT:
    r = 53u;
    name = "split pane";
    break;
  case SZ_A11Y_PROGRESS:
  case SZ_A11Y_CIRCULAR:
    r = 42u;
    name = "progress bar";
    break;
  case SZ_A11Y_LIST:
  case SZ_A11Y_BREADCRUMB:
    r = 31u;
    name = "list";
    break;
  case SZ_A11Y_LISTBOX:
    r = 98u;
    name = "list box";
    break;
  case SZ_A11Y_LIST_TILE:
  case SZ_A11Y_OPTION:
    r = 32u;
    name = "list item";
    break;
  case SZ_A11Y_SCROLL:
    r = 49u;
    name = "scroll pane";
    break;
  case SZ_A11Y_COMBOBOX:
    r = 11u;
    name = "combo box";
    break;
  case SZ_A11Y_OVERLAY:
    r = 16u;
    name = "dialog";
    break;
  case SZ_A11Y_TOOLTIP:
    r = 64u;
    name = "tool tip";
    break;
  case SZ_A11Y_TAB:
    r = 37u;
    name = "page tab";
    break;
  case SZ_A11Y_TAB_LIST:
    r = 38u;
    name = "page tab list";
    break;
  case SZ_A11Y_TAB_PANEL:
    r = 84u;
    name = "page";
    break;
  case SZ_A11Y_APP_BAR:
    r = 63u;
    name = "tool bar";
    break;
  case SZ_A11Y_LINK:
  case SZ_A11Y_NAV_TILE:
    r = 88u;
    name = "link";
    break;
  case SZ_A11Y_DIVIDER:
  case SZ_A11Y_VDIV:
    r = 50u;
    name = "separator";
    break;
  case SZ_A11Y_REGION:
    r = 110u;
    name = "landmark";
    break;
  default:
    r = 39u;
    name = "panel";
    break;
  }
  *out_role = r;
  *out_name = name;
}

static void a11y_state_set_bit(dbus_uint32_t words[2], int bit) {
  if (bit >= 0 && bit < 32)
    words[0] |= (1u << bit);
  else if (bit >= 32 && bit < 64)
    words[1] |= (1u << (bit - 32));
}

static void a11y_state_words(int is_root, const SzA11yNode *node,
                             dbus_uint32_t words[2]) {
  words[0] = 0u;
  words[1] = 0u;
  if (is_root) {
    a11y_state_set_bit(words, ATSPI_STATE_ACTIVE);
    a11y_state_set_bit(words, ATSPI_STATE_ENABLED);
    a11y_state_set_bit(words, ATSPI_STATE_SENSITIVE);
    a11y_state_set_bit(words, ATSPI_STATE_SHOWING);
    a11y_state_set_bit(words, ATSPI_STATE_VISIBLE);
    return;
  }
  if (!node) {
    a11y_state_set_bit(words, ATSPI_STATE_DEFUNCT);
    return;
  }
  a11y_state_set_bit(words, ATSPI_STATE_ENABLED);
  a11y_state_set_bit(words, ATSPI_STATE_SENSITIVE);
  a11y_state_set_bit(words, ATSPI_STATE_SHOWING);
  a11y_state_set_bit(words, ATSPI_STATE_VISIBLE);
  if (node->actions & SZ_A11Y_FOCUS)
    a11y_state_set_bit(words, ATSPI_STATE_FOCUSABLE);
  if (node->focused)
    a11y_state_set_bit(words, ATSPI_STATE_FOCUSED);
  if (node->editable) {
    a11y_state_set_bit(words, ATSPI_STATE_EDITABLE);
    a11y_state_set_bit(words, ATSPI_STATE_SELECTABLE_TEXT);
    a11y_state_set_bit(words, node->multiline ? ATSPI_STATE_MULTI_LINE
                                              : ATSPI_STATE_SINGLE_LINE);
  }
  if (node->checked >= 0) {
    a11y_state_set_bit(words, ATSPI_STATE_CHECKABLE);
    if (node->checked != 0)
      a11y_state_set_bit(words, ATSPI_STATE_CHECKED);
  }
  if (node->selected >= 0) {
    a11y_state_set_bit(words, ATSPI_STATE_SELECTABLE);
    if (node->selected != 0)
      a11y_state_set_bit(words, ATSPI_STATE_SELECTED);
  }
  if (node->expanded >= 0) {
    a11y_state_set_bit(words, ATSPI_STATE_EXPANDABLE);
    a11y_state_set_bit(words, node->expanded != 0 ? ATSPI_STATE_EXPANDED
                                                  : ATSPI_STATE_COLLAPSED);
  }
  if (node->role == SZ_A11Y_OVERLAY) {
    a11y_state_set_bit(words, ATSPI_STATE_MODAL);
    a11y_state_set_bit(words, ATSPI_STATE_ACTIVE);
  }
  if (node->description && node->description[0])
    a11y_state_set_bit(words, ATSPI_STATE_HAS_TOOLTIP);
  if (node->role == SZ_A11Y_COMBOBOX)
    a11y_state_set_bit(words, ATSPI_STATE_HAS_POPUP);
}

static dbus_int32_t a11y_child_count(uintptr_t parent_id) {
  size_t i, n;
  dbus_int32_t count = 0;
  if (!g_a11y_snapshot)
    return 0;
  n = sz_a11y_snapshot_count(g_a11y_snapshot);
  for (i = 0; i < n; i++) {
    const SzA11yNode *c = sz_a11y_snapshot_node(g_a11y_snapshot, i);
    if (c && c->parent_id == parent_id && count < INT32_MAX)
      count++;
  }
  return count;
}

static const SzA11yNode *a11y_child_at(uintptr_t parent_id,
                                       dbus_int32_t child_idx) {
  size_t i, n;
  dbus_int32_t cur = 0;
  if (!g_a11y_snapshot || child_idx < 0)
    return NULL;
  n = sz_a11y_snapshot_count(g_a11y_snapshot);
  for (i = 0; i < n; i++) {
    const SzA11yNode *c = sz_a11y_snapshot_node(g_a11y_snapshot, i);
    if (c && c->parent_id == parent_id) {
      if (cur == child_idx)
        return c;
      cur++;
    }
  }
  return NULL;
}

static dbus_int32_t a11y_index_in_parent(int is_root, const SzA11yNode *node) {
  size_t i, n;
  dbus_int32_t idx = 0;
  if (is_root)
    return g_a11y_plug_path ? 0 : -1;
  if (!node || !g_a11y_snapshot)
    return -1;
  n = sz_a11y_snapshot_count(g_a11y_snapshot);
  for (i = 0; i < n; i++) {
    const SzA11yNode *c = sz_a11y_snapshot_node(g_a11y_snapshot, i);
    if (c && c->parent_id == node->parent_id) {
      if (c->id == node->id)
        return idx;
      idx++;
    }
  }
  return -1;
}

static void a11y_window_origin(int32_t *out_x, int32_t *out_y) {
  *out_x = 0;
  *out_y = 0;
  if (g_dpy && g_win && g_ready) {
    int sx = 0, sy = 0;
    Window child = 0;
    if (XTranslateCoordinates(g_dpy, g_win, DefaultRootWindow(g_dpy), 0, 0, &sx,
                              &sy, &child)) {
      *out_x = (int32_t)sx;
      *out_y = (int32_t)sy;
    }
  }
}

static void a11y_extents(int is_root, const SzA11yNode *node,
                         dbus_uint32_t coord_type, dbus_int32_t *out_x,
                         dbus_int32_t *out_y, dbus_int32_t *out_w,
                         dbus_int32_t *out_h) {
  int32_t wx = 0, wy = 0, lx, ly, rx, ry;
  if (!is_root && !node) {
    *out_x = 0;
    *out_y = 0;
    *out_w = 0;
    *out_h = 0;
    return;
  }
  a11y_window_origin(&wx, &wy);
  if (is_root) {
    lx = 0;
    ly = 0;
    rx = g_a11y_w > 0 ? g_a11y_w : (g_w > 0 ? g_w : 0);
    ry = g_a11y_h > 0 ? g_a11y_h : (g_h > 0 ? g_h : 0);
  } else {
    lx = a11y_floor_i32(node->bounds.x);
    ly = a11y_floor_i32(node->bounds.y);
    rx = a11y_ceil_i32(node->bounds.x + node->bounds.w);
    ry = a11y_ceil_i32(node->bounds.y + node->bounds.h);
    if (rx < lx)
      rx = lx;
    if (ry < ly)
      ry = ly;
  }
  *out_w = rx - lx;
  *out_h = ry - ly;
  if (coord_type == ATSPI_COORD_SCREEN) {
    *out_x = wx + lx;
    *out_y = wy + ly;
  } else if (coord_type == ATSPI_COORD_PARENT) {
    if (is_root) {
      *out_x = wx;
      *out_y = wy;
    } else if (node->parent_id != 0) {
      const SzA11yNode *parent = a11y_find_node(g_a11y_snapshot, node->parent_id);
      int32_t px = parent ? a11y_floor_i32(parent->bounds.x) : 0;
      int32_t py = parent ? a11y_floor_i32(parent->bounds.y) : 0;
      *out_x = lx - px;
      *out_y = ly - py;
    } else {
      *out_x = lx;
      *out_y = ly;
    }
  } else {
    *out_x = lx;
    *out_y = ly;
  }
}

static const char *a11y_self_bus(void) {
  const char *u = g_a11y_bus ? dbus_bus_get_unique_name(g_a11y_bus) : NULL;
  return u ? u : "";
}

static int a11y_append_ref(DBusMessageIter *iter, const char *bus_name,
                           const char *obj_path) {
  DBusMessageIter st;
  const char *b = bus_name ? bus_name : "";
  const char *p = (obj_path && obj_path[0] == '/') ? obj_path : ATSPI_NULL_PATH;
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &st))
    return 0;
  if (!dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &b) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_OBJECT_PATH, &p)) {
    dbus_message_iter_close_container(iter, &st);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &st);
}

static void a11y_parent_ref(int is_root, const SzA11yNode *node,
                            const char **out_bus, char *path_buf, size_t cap) {
  if (is_root) {
    if (g_a11y_plug_bus && g_a11y_plug_path && g_a11y_plug_path[0] == '/') {
      *out_bus = g_a11y_plug_bus;
      snprintf(path_buf, cap, "%s", g_a11y_plug_path);
    } else {
      *out_bus = "";
      snprintf(path_buf, cap, "%s", ATSPI_NULL_PATH);
    }
    return;
  }
  if (!node) {
    *out_bus = "";
    snprintf(path_buf, cap, "%s", ATSPI_NULL_PATH);
    return;
  }
  if (node->parent_id != 0 && !a11y_find_node(g_a11y_snapshot, node->parent_id)) {
    *out_bus = a11y_self_bus();
    snprintf(path_buf, cap, "%s", ATSPI_ROOT_PATH);
    return;
  }
  *out_bus = a11y_self_bus();
  a11y_make_path(node->parent_id, path_buf, cap);
}

static int a11y_append_interfaces(DBusMessageIter *iter, int is_root,
                                  const SzA11yNode *node) {
  DBusMessageIter arr;
  const char *ifaces[8];
  int n = 0, i;
  ifaces[n++] = ATSPI_IFACE_ACCESSIBLE;
  ifaces[n++] = ATSPI_IFACE_COMPONENT;
  if (is_root) {
    ifaces[n++] = ATSPI_IFACE_APPLICATION;
  } else if (node) {
    if (a11y_action_count(node) > 0)
      ifaces[n++] = ATSPI_IFACE_ACTION;
    if (a11y_has_value(node))
      ifaces[n++] = ATSPI_IFACE_VALUE;
    if (a11y_has_text(node))
      ifaces[n++] = ATSPI_IFACE_TEXT;
    if (node->editable)
      ifaces[n++] = ATSPI_IFACE_EDITABLE;
  }
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &arr))
    return 0;
  for (i = 0; i < n; i++) {
    if (!dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &ifaces[i])) {
      dbus_message_iter_close_container(iter, &arr);
      return 0;
    }
  }
  return dbus_message_iter_close_container(iter, &arr);
}

static int a11y_append_states(DBusMessageIter *iter, int is_root,
                              const SzA11yNode *node) {
  DBusMessageIter arr;
  dbus_uint32_t words[2];
  a11y_state_words(is_root, node, words);
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "u", &arr))
    return 0;
  if (!dbus_message_iter_append_basic(&arr, DBUS_TYPE_UINT32, &words[0]) ||
      !dbus_message_iter_append_basic(&arr, DBUS_TYPE_UINT32, &words[1])) {
    dbus_message_iter_close_container(iter, &arr);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &arr);
}

static int a11y_append_variant_str(DBusMessageIter *iter, const char *val) {
  DBusMessageIter v;
  const char *s = val ? val : "";
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &v))
    return 0;
  if (!dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s)) {
    dbus_message_iter_close_container(iter, &v);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &v);
}

static int a11y_append_variant_i32(DBusMessageIter *iter, dbus_int32_t val) {
  DBusMessageIter v;
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &v))
    return 0;
  if (!dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &val)) {
    dbus_message_iter_close_container(iter, &v);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &v);
}

static int a11y_append_variant_f64(DBusMessageIter *iter, double val) {
  DBusMessageIter v;
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "d", &v))
    return 0;
  if (!dbus_message_iter_append_basic(&v, DBUS_TYPE_DOUBLE, &val)) {
    dbus_message_iter_close_container(iter, &v);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &v);
}

static int a11y_append_variant_ref(DBusMessageIter *iter, const char *bus_name,
                                   const char *obj_path) {
  DBusMessageIter v;
  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(so)", &v))
    return 0;
  if (!a11y_append_ref(&v, bus_name, obj_path)) {
    dbus_message_iter_close_container(iter, &v);
    return 0;
  }
  return dbus_message_iter_close_container(iter, &v);
}

static int a11y_prop_value(DBusMessageIter *out, const char *iface,
                           const char *prop, int is_root,
                           const SzA11yNode *node, uintptr_t self_id) {
  if (!strcmp(iface, ATSPI_IFACE_ACCESSIBLE)) {
    if (!strcmp(prop, "Name")) {
      const char *name = is_root ? (g_win_title ? g_win_title : "Scuzz Lang")
                                 : (node && node->label ? node->label : "");
      return a11y_append_variant_str(out, name);
    }
    if (!strcmp(prop, "Description") || !strcmp(prop, "HelpText")) {
      const char *desc =
          (!is_root && node && node->description) ? node->description : "";
      return a11y_append_variant_str(out, desc);
    }
    if (!strcmp(prop, "Parent")) {
      const char *pbus = "";
      char ppath[96];
      a11y_parent_ref(is_root, node, &pbus, ppath, sizeof(ppath));
      return a11y_append_variant_ref(out, pbus, ppath);
    }
    if (!strcmp(prop, "ChildCount")) {
      dbus_int32_t n =
          (is_root || node) ? a11y_child_count(is_root ? 0 : self_id) : 0;
      return a11y_append_variant_i32(out, n);
    }
    if (!strcmp(prop, "Locale"))
      return a11y_append_variant_str(out, "C");
    if (!strcmp(prop, "AccessibleId")) {
      char idbuf[64];
      if (is_root)
        snprintf(idbuf, sizeof(idbuf), "root");
      else if (node)
        snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)self_id);
      else
        idbuf[0] = '\0';
      return a11y_append_variant_str(out, idbuf);
    }
    return 0;
  }
  if (!strcmp(iface, ATSPI_IFACE_APPLICATION) && is_root) {
    if (!strcmp(prop, "ToolkitName"))
      return a11y_append_variant_str(out, "scuzz");
    if (!strcmp(prop, "Version"))
      return a11y_append_variant_str(out, "1.0");
    if (!strcmp(prop, "AtspiVersion"))
      return a11y_append_variant_str(out, "2.1");
    if (!strcmp(prop, "Id"))
      return a11y_append_variant_i32(out, g_a11y_app_id);
    return 0;
  }
  if (!strcmp(iface, ATSPI_IFACE_ACTION)) {
    if (!strcmp(prop, "NActions"))
      return a11y_append_variant_i32(out, a11y_action_count(node));
    return 0;
  }
  if (!strcmp(iface, ATSPI_IFACE_VALUE) && a11y_has_value(node)) {
    if (!strcmp(prop, "CurrentValue"))
      return a11y_append_variant_f64(out, (double)node->number);
    if (!strcmp(prop, "MinimumValue"))
      return a11y_append_variant_f64(out, (double)node->minimum);
    if (!strcmp(prop, "MaximumValue"))
      return a11y_append_variant_f64(out, (double)node->maximum);
    if (!strcmp(prop, "MinimumIncrement"))
      return a11y_append_variant_f64(out, (double)node->step);
    if (!strcmp(prop, "Text"))
      return a11y_append_variant_str(out, node->value ? node->value : "");
    return 0;
  }
  if (!strcmp(iface, ATSPI_IFACE_TEXT) && !is_root && node) {
    const char *txt = a11y_node_text(node);
    if (!strcmp(prop, "CharacterCount"))
      return a11y_append_variant_i32(out, utf8_char_count(txt));
    if (!strcmp(prop, "CaretOffset")) {
      int s = 0, e = 0;
      a11y_node_selection_bytes(node, &s, &e);
      return a11y_append_variant_i32(out,
                                     node->editable ? utf8_byte_to_char(txt, e)
                                                    : 0);
    }
    return 0;
  }
  return 0;
}

static int a11y_write_prop_named(DBusMessageIter *entry, const char *iface,
                                 const char *prop, int is_root,
                                 const SzA11yNode *node, uintptr_t self_id) {
  DBusMessageIter kv;
  if (!dbus_message_iter_open_container(entry, DBUS_TYPE_DICT_ENTRY, NULL, &kv))
    return 0;
  if (!dbus_message_iter_append_basic(&kv, DBUS_TYPE_STRING, &prop) ||
      !a11y_prop_value(&kv, iface, prop, is_root, node, self_id)) {
    dbus_message_iter_close_container(entry, &kv);
    return 0;
  }
  return dbus_message_iter_close_container(entry, &kv);
}

static DBusMessage *a11y_handle_properties(DBusMessage *msg, int is_root,
                                           const SzA11yNode *node,
                                           uintptr_t self_id) {
  const char *member = dbus_message_get_member(msg);
  DBusMessageIter args;
  if (!member)
    return NULL;
  if (!strcmp(member, "Get")) {
    const char *iface = NULL, *prop = NULL;
    DBusMessage *reply;
    DBusMessageIter out;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Invalid property arguments");
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!a11y_prop_value(&out, iface, prop, is_root, node, self_id)) {
      dbus_message_unref(reply);
      return dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, prop);
    }
    return reply;
  }
  if (!strcmp(member, "GetAll")) {
    const char *iface = NULL;
    DBusMessage *reply;
    DBusMessageIter out, dict;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Invalid GetAll arguments");
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
      dbus_message_unref(reply);
      return NULL;
    }
    if (!strcmp(iface, ATSPI_IFACE_ACCESSIBLE)) {
      (void)a11y_write_prop_named(&dict, iface, "Name", is_root, node, self_id);
      (void)a11y_write_prop_named(&dict, iface, "Description", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "Parent", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "ChildCount", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "Locale", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "AccessibleId", is_root, node,
                                  self_id);
    } else if (!strcmp(iface, ATSPI_IFACE_APPLICATION) && is_root) {
      (void)a11y_write_prop_named(&dict, iface, "ToolkitName", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "Version", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "AtspiVersion", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "Id", is_root, node, self_id);
    } else if (!strcmp(iface, ATSPI_IFACE_ACTION)) {
      (void)a11y_write_prop_named(&dict, iface, "NActions", is_root, node,
                                  self_id);
    } else if (!strcmp(iface, ATSPI_IFACE_VALUE) && a11y_has_value(node)) {
      (void)a11y_write_prop_named(&dict, iface, "CurrentValue", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "MinimumValue", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "MaximumValue", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "MinimumIncrement", is_root,
                                  node, self_id);
      (void)a11y_write_prop_named(&dict, iface, "Text", is_root, node, self_id);
    } else if (!strcmp(iface, ATSPI_IFACE_TEXT) && !is_root && node) {
      (void)a11y_write_prop_named(&dict, iface, "CharacterCount", is_root, node,
                                  self_id);
      (void)a11y_write_prop_named(&dict, iface, "CaretOffset", is_root, node,
                                  self_id);
    }
    dbus_message_iter_close_container(&out, &dict);
    return reply;
  }
  if (!strcmp(member, "Set")) {
    const char *iface = NULL, *prop = NULL;
    DBusMessageIter var;
    if (!dbus_message_iter_init(msg, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Expected iface");
    dbus_message_iter_get_basic(&args, &iface);
    if (!dbus_message_iter_next(&args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Expected prop");
    dbus_message_iter_get_basic(&args, &prop);
    if (!dbus_message_iter_next(&args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "Expected variant");
    dbus_message_iter_recurse(&args, &var);
    if (!strcmp(iface, ATSPI_IFACE_APPLICATION) && is_root &&
        !strcmp(prop, "Id") &&
        dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_INT32) {
      dbus_message_iter_get_basic(&var, &g_a11y_app_id);
      return dbus_message_new_method_return(msg);
    }
    if (!strcmp(iface, ATSPI_IFACE_VALUE) && !strcmp(prop, "CurrentValue")) {
      int vtype = dbus_message_iter_get_arg_type(&var);
      int64_t next_val;
      if (!node || !a11y_has_value(node) ||
          !(node->actions & SZ_A11Y_SET_VALUE))
        return dbus_message_new_error(msg, DBUS_ERROR_PROPERTY_READ_ONLY,
                                      "Value is not writable");
      if (vtype == DBUS_TYPE_DOUBLE) {
        double d = 0.0;
        dbus_message_iter_get_basic(&var, &d);
        if (!(d == d))
          return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                        "Value is NaN");
        next_val = a11y_clamp_double_i64(d, node->minimum, node->maximum);
      } else if (vtype == DBUS_TYPE_INT64) {
        dbus_int64_t raw = 0;
        dbus_message_iter_get_basic(&var, &raw);
        next_val = (int64_t)raw;
        if (next_val < node->minimum)
          next_val = node->minimum;
        if (next_val > node->maximum)
          next_val = node->maximum;
      } else if (vtype == DBUS_TYPE_INT32) {
        dbus_int32_t raw = 0;
        dbus_message_iter_get_basic(&var, &raw);
        next_val = (int64_t)raw;
        if (next_val < node->minimum)
          next_val = node->minimum;
        if (next_val > node->maximum)
          next_val = node->maximum;
      } else {
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Expected numeric CurrentValue");
      }
      if (!a11y_enqueue(node->id, SZ_A11Y_SET_VALUE, NULL, 0, 0, next_val))
        return dbus_message_new_error(msg, DBUS_ERROR_NO_MEMORY, "Queue full");
      return dbus_message_new_method_return(msg);
    }
    return dbus_message_new_error(msg, DBUS_ERROR_PROPERTY_READ_ONLY, prop);
  }
  return NULL;
}

static DBusMessage *a11y_reply_bool(DBusMessage *msg, int ok) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  dbus_bool_t b = ok ? TRUE : FALSE;
  if (!reply)
    return NULL;
  if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &b,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(reply);
    return NULL;
  }
  return reply;
}

static DBusMessage *a11y_reply_str(DBusMessage *msg, const char *s) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  const char *v = s ? s : "";
  if (!reply)
    return NULL;
  if (!dbus_message_append_args(reply, DBUS_TYPE_STRING, &v,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(reply);
    return NULL;
  }
  return reply;
}

static DBusMessage *a11y_reply_i32(DBusMessage *msg, dbus_int32_t v) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return NULL;
  if (!dbus_message_append_args(reply, DBUS_TYPE_INT32, &v,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(reply);
    return NULL;
  }
  return reply;
}

static DBusMessage *a11y_reply_u32(DBusMessage *msg, dbus_uint32_t v) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return NULL;
  if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &v,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(reply);
    return NULL;
  }
  return reply;
}

static DBusMessage *a11y_reply_ref(DBusMessage *msg, const char *bus_name,
                                   const char *obj_path) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter out;
  if (!reply)
    return NULL;
  dbus_message_iter_init_append(reply, &out);
  if (!a11y_append_ref(&out, bus_name, obj_path)) {
    dbus_message_unref(reply);
    return NULL;
  }
  return reply;
}

static DBusMessage *a11y_handle_accessible(DBusMessage *msg, int is_root,
                                           const SzA11yNode *node,
                                           uintptr_t self_id) {
  const char *member = dbus_message_get_member(msg);
  if (!member)
    return NULL;
  if (!strcmp(member, "GetRole")) {
    dbus_uint32_t role = 0;
    const char *rname = NULL;
    a11y_role_info(is_root, node, &role, &rname);
    return a11y_reply_u32(msg, role);
  }
  if (!strcmp(member, "GetRoleName") ||
      !strcmp(member, "GetLocalizedRoleName")) {
    dbus_uint32_t role = 0;
    const char *rname = NULL;
    a11y_role_info(is_root, node, &role, &rname);
    return a11y_reply_str(msg, rname);
  }
  if (!strcmp(member, "GetState")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out;
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!a11y_append_states(&out, is_root, node)) {
      dbus_message_unref(reply);
      return NULL;
    }
    return reply;
  }
  if (!strcmp(member, "GetIndexInParent"))
    return a11y_reply_i32(msg, a11y_index_in_parent(is_root, node));
  if (!strcmp(member, "GetChildAtIndex")) {
    dbus_int32_t idx = -1;
    const SzA11yNode *child;
    char cpath[96];
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &idx,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected child index");
    if (!is_root && !node)
      return a11y_reply_ref(msg, "", ATSPI_NULL_PATH);
    child = a11y_child_at(is_root ? 0 : self_id, idx);
    if (!child)
      return a11y_reply_ref(msg, "", ATSPI_NULL_PATH);
    a11y_make_path(child->id, cpath, sizeof(cpath));
    return a11y_reply_ref(msg, a11y_self_bus(), cpath);
  }
  if (!strcmp(member, "GetChildren")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out, arr;
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(so)", &arr)) {
      dbus_message_unref(reply);
      return NULL;
    }
    if ((is_root || node) && g_a11y_snapshot) {
      uintptr_t pid = is_root ? 0 : self_id;
      size_t i, n = sz_a11y_snapshot_count(g_a11y_snapshot);
      for (i = 0; i < n; i++) {
        const SzA11yNode *c = sz_a11y_snapshot_node(g_a11y_snapshot, i);
        if (c && c->parent_id == pid) {
          char cpath[96];
          a11y_make_path(c->id, cpath, sizeof(cpath));
          (void)a11y_append_ref(&arr, a11y_self_bus(), cpath);
        }
      }
    }
    dbus_message_iter_close_container(&out, &arr);
    return reply;
  }
  if (!strcmp(member, "GetRelationSet")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out, arr;
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(ua(so))",
                                          &arr)) {
      dbus_message_unref(reply);
      return NULL;
    }
    dbus_message_iter_close_container(&out, &arr);
    return reply;
  }
  if (!strcmp(member, "GetAttributes")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out, dict;
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{ss}", &dict)) {
      dbus_message_unref(reply);
      return NULL;
    }
    dbus_message_iter_close_container(&out, &dict);
    return reply;
  }
  if (!strcmp(member, "GetApplication"))
    return a11y_reply_ref(msg, a11y_self_bus(), ATSPI_ROOT_PATH);
  if (!strcmp(member, "GetInterfaces")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out;
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!a11y_append_interfaces(&out, is_root, node)) {
      dbus_message_unref(reply);
      return NULL;
    }
    return reply;
  }
  return NULL;
}

static DBusMessage *a11y_handle_component(DBusMessage *msg, int is_root,
                                          const SzA11yNode *node,
                                          uintptr_t self_id) {
  const char *member = dbus_message_get_member(msg);
  if (!member)
    return NULL;
  if (!strcmp(member, "Contains")) {
    dbus_int32_t x = 0, y = 0, bx, by, bw, bh;
    dbus_uint32_t ct = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32,
                               &y, DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected x, y, coord_type");
    if (!is_root && !node)
      return a11y_reply_bool(msg, 0);
    a11y_extents(is_root, node, ct, &bx, &by, &bw, &bh);
    return a11y_reply_bool(msg,
                           bw > 0 && bh > 0 && x >= bx && y >= by &&
                               (int64_t)x < (int64_t)bx + (int64_t)bw &&
                               (int64_t)y < (int64_t)by + (int64_t)bh);
  }
  if (!strcmp(member, "GetAccessibleAtPoint")) {
    dbus_int32_t x = 0, y = 0, bx, by, bw, bh;
    dbus_uint32_t ct = 0;
    int64_t lx, ly;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &x, DBUS_TYPE_INT32,
                               &y, DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected x, y, coord_type");
    if ((!is_root && !node) || !g_a11y_snapshot)
      return a11y_reply_ref(msg, "", ATSPI_NULL_PATH);
    a11y_extents(is_root, node, ct, &bx, &by, &bw, &bh);
    {
      dbus_int32_t wx, wy, ww, wh;
      a11y_extents(is_root, node, ATSPI_COORD_WINDOW, &wx, &wy, &ww, &wh);
      lx = (int64_t)x - (int64_t)bx + (int64_t)wx;
      ly = (int64_t)y - (int64_t)by + (int64_t)wy;
    }
    {
      uintptr_t pid = is_root ? 0 : self_id;
      size_t n = sz_a11y_snapshot_count(g_a11y_snapshot);
      while (n > 0) {
        const SzA11yNode *c = sz_a11y_snapshot_node(g_a11y_snapshot, --n);
        if (c && c->parent_id == pid) {
          dbus_int32_t cx, cy, cw, ch;
          a11y_extents(0, c, ATSPI_COORD_WINDOW, &cx, &cy, &cw, &ch);
          if (cw > 0 && ch > 0 && lx >= cx && ly >= cy &&
              lx < (int64_t)cx + (int64_t)cw &&
              ly < (int64_t)cy + (int64_t)ch) {
            char cpath[96];
            a11y_make_path(c->id, cpath, sizeof(cpath));
            return a11y_reply_ref(msg, a11y_self_bus(), cpath);
          }
        }
      }
    }
    return a11y_reply_ref(msg, "", ATSPI_NULL_PATH);
  }
  if (!strcmp(member, "GetExtents")) {
    dbus_uint32_t ct = 0;
    dbus_int32_t bx, by, bw, bh;
    DBusMessage *reply;
    DBusMessageIter out, st;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &ct,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected coord_type");
    a11y_extents(is_root, node, ct, &bx, &by, &bw, &bh);
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_STRUCT, NULL, &st)) {
      dbus_message_unref(reply);
      return NULL;
    }
    (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &bx);
    (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &by);
    (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &bw);
    (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &bh);
    dbus_message_iter_close_container(&out, &st);
    return reply;
  }
  if (!strcmp(member, "GetPosition")) {
    dbus_uint32_t ct = 0;
    dbus_int32_t bx, by, bw, bh;
    DBusMessage *reply;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_UINT32, &ct,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected coord_type");
    a11y_extents(is_root, node, ct, &bx, &by, &bw, &bh);
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    (void)dbus_message_append_args(reply, DBUS_TYPE_INT32, &bx, DBUS_TYPE_INT32,
                                   &by, DBUS_TYPE_INVALID);
    return reply;
  }
  if (!strcmp(member, "GetSize")) {
    dbus_int32_t bx, by, bw, bh;
    DBusMessage *reply;
    a11y_extents(is_root, node, ATSPI_COORD_WINDOW, &bx, &by, &bw, &bh);
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    (void)dbus_message_append_args(reply, DBUS_TYPE_INT32, &bw, DBUS_TYPE_INT32,
                                   &bh, DBUS_TYPE_INVALID);
    return reply;
  }
  if (!strcmp(member, "GetLayer")) {
    dbus_uint32_t layer = ATSPI_LAYER_INVALID;
    if (is_root)
      layer = ATSPI_LAYER_WINDOW;
    else if (node) {
      if (node->role == SZ_A11Y_OVERLAY)
        layer = ATSPI_LAYER_OVERLAY;
      else if (node->role == SZ_A11Y_TOOLTIP || node->role == SZ_A11Y_LISTBOX ||
               node->role == SZ_A11Y_OPTION)
        layer = ATSPI_LAYER_POPUP;
      else
        layer = ATSPI_LAYER_WIDGET;
    }
    return a11y_reply_u32(msg, layer);
  }
  if (!strcmp(member, "GetMDIZOrder")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_int16_t z = 0;
    if (!reply)
      return NULL;
    (void)dbus_message_append_args(reply, DBUS_TYPE_INT16, &z,
                                   DBUS_TYPE_INVALID);
    return reply;
  }
  if (!strcmp(member, "GrabFocus")) {
    if (is_root || !node || !(node->actions & SZ_A11Y_FOCUS))
      return a11y_reply_bool(msg, 0);
    return a11y_reply_bool(
        msg, a11y_enqueue(node->id, SZ_A11Y_FOCUS, NULL, 0, 0, 0));
  }
  if (!strcmp(member, "GetAlpha")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    double alpha = (is_root || node) ? 1.0 : 0.0;
    if (!reply)
      return NULL;
    (void)dbus_message_append_args(reply, DBUS_TYPE_DOUBLE, &alpha,
                                   DBUS_TYPE_INVALID);
    return reply;
  }
  return NULL;
}

static DBusMessage *a11y_handle_action(DBusMessage *msg,
                                       const SzA11yNode *node) {
  const char *member = dbus_message_get_member(msg);
  dbus_int32_t idx = -1;
  if (!member)
    return NULL;
  if (!strcmp(member, "GetActions")) {
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter out, arr;
    dbus_int32_t i, n = a11y_action_count(node);
    const char *empty = "";
    if (!reply)
      return NULL;
    dbus_message_iter_init_append(reply, &out);
    if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "(sss)", &arr)) {
      dbus_message_unref(reply);
      return NULL;
    }
    for (i = 0; i < n; i++) {
      const A11yActionSpec *spec = a11y_action_at(node, i);
      DBusMessageIter st;
      if (spec &&
          dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, NULL, &st)) {
        (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &spec->name);
        (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &spec->name);
        (void)dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
        dbus_message_iter_close_container(&arr, &st);
      }
    }
    dbus_message_iter_close_container(&out, &arr);
    return reply;
  }
  if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &idx,
                             DBUS_TYPE_INVALID))
    return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                  "Expected action index");
  if (!strcmp(member, "GetName") || !strcmp(member, "GetLocalizedName") ||
      !strcmp(member, "GetDescription")) {
    const A11yActionSpec *spec = a11y_action_at(node, idx);
    return a11y_reply_str(msg, spec ? spec->name : "");
  }
  if (!strcmp(member, "GetKeyBinding"))
    return a11y_reply_str(msg, "");
  if (!strcmp(member, "DoAction")) {
    const A11yActionSpec *spec = a11y_action_at(node, idx);
    if (!node || !spec)
      return a11y_reply_bool(msg, 0);
    return a11y_reply_bool(msg,
                           a11y_enqueue(node->id, spec->bit, NULL, 0, 0, 0));
  }
  return NULL;
}

static char *a11y_slice_chars(const char *txt, dbus_int32_t start_c,
                              dbus_int32_t end_c) {
  dbus_int32_t nchars = utf8_char_count(txt);
  size_t b0, b1, n;
  char *out;
  if (start_c < 0)
    start_c = 0;
  if (end_c < 0 || end_c > nchars)
    end_c = nchars;
  if (start_c > end_c)
    start_c = end_c;
  b0 = utf8_char_to_byte(txt, start_c);
  b1 = utf8_char_to_byte(txt, end_c);
  n = b1 >= b0 ? (b1 - b0) : 0;
  out = (char *)malloc(n + 1);
  if (!out)
    return NULL;
  if (n > 0)
    memcpy(out, txt + b0, n);
  out[n] = '\0';
  return out;
}

static DBusMessage *a11y_handle_text(DBusMessage *msg, const SzA11yNode *node) {
  const char *member = dbus_message_get_member(msg);
  const char *txt = a11y_node_text(node);
  dbus_int32_t nchars = utf8_char_count(txt);
  if (!member)
    return NULL;
  if (!strcmp(member, "GetText")) {
    dbus_int32_t s = 0, e = -1;
    char *slice;
    DBusMessage *reply;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32,
                               &e, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected start, end");
    if (!node)
      return a11y_reply_str(msg, "");
    slice = a11y_slice_chars(txt, s, e);
    reply = a11y_reply_str(msg, slice ? slice : "");
    free(slice);
    return reply;
  }
  if (!strcmp(member, "GetCharacterAtOffset")) {
    dbus_int32_t off = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &off,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected offset");
    return a11y_reply_i32(msg, node ? utf8_char_at(txt, off) : 0);
  }
  if (!strcmp(member, "SetCaretOffset")) {
    dbus_int32_t off = 0;
    int b;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &off,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected offset");
    if (!node || !node->editable || !(node->actions & SZ_A11Y_SET_SELECTION))
      return a11y_reply_bool(msg, 0);
    if (off < 0 || off > nchars)
      off = (off == -1) ? nchars : (off < 0 ? 0 : nchars);
    b = (int)utf8_char_to_byte(txt, off);
    return a11y_reply_bool(
        msg, a11y_enqueue(node->id, SZ_A11Y_SET_SELECTION, NULL, b, b, 0));
  }
  if (!strcmp(member, "GetNSelections")) {
    int s = 0, e = 0;
    a11y_node_selection_bytes(node, &s, &e);
    return a11y_reply_i32(msg, (node && node->editable && s != e) ? 1 : 0);
  }
  if (!strcmp(member, "GetSelection")) {
    dbus_int32_t sel_num = 0, cs = 0, ce = 0;
    int bs = 0, be = 0;
    DBusMessage *reply;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &sel_num,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected selection index");
    a11y_node_selection_bytes(node, &bs, &be);
    if (node && node->editable && sel_num == 0 && bs != be) {
      int lo = bs < be ? bs : be;
      int hi = bs < be ? be : bs;
      cs = utf8_byte_to_char(txt, lo);
      ce = utf8_byte_to_char(txt, hi);
    }
    reply = dbus_message_new_method_return(msg);
    if (!reply)
      return NULL;
    (void)dbus_message_append_args(reply, DBUS_TYPE_INT32, &cs, DBUS_TYPE_INT32,
                                   &ce, DBUS_TYPE_INVALID);
    return reply;
  }
  if (!strcmp(member, "AddSelection") || !strcmp(member, "SetSelection")) {
    dbus_int32_t sel_num = 0, s = 0, e = 0;
    int bs, be;
    if (!strcmp(member, "AddSelection")) {
      if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &s,
                                 DBUS_TYPE_INT32, &e, DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Expected start, end");
    } else {
      if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &sel_num,
                                 DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32, &e,
                                 DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Expected selection_num, start, end");
    }
    if (!node || !node->editable || !(node->actions & SZ_A11Y_SET_SELECTION) ||
        sel_num != 0)
      return a11y_reply_bool(msg, 0);
    if (s < 0)
      s = (s == -1) ? nchars : 0;
    if (e < 0)
      e = (e == -1) ? nchars : 0;
    if (s > nchars)
      s = nchars;
    if (e > nchars)
      e = nchars;
    bs = (int)utf8_char_to_byte(txt, s);
    be = (int)utf8_char_to_byte(txt, e);
    return a11y_reply_bool(
        msg, a11y_enqueue(node->id, SZ_A11Y_SET_SELECTION, NULL, bs, be, 0));
  }
  if (!strcmp(member, "RemoveSelection")) {
    dbus_int32_t sel_num = 0;
    int bs = 0, be = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &sel_num,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected selection index");
    if (!node || !node->editable || !(node->actions & SZ_A11Y_SET_SELECTION) ||
        sel_num != 0)
      return a11y_reply_bool(msg, 0);
    a11y_node_selection_bytes(node, &bs, &be);
    return a11y_reply_bool(
        msg, a11y_enqueue(node->id, SZ_A11Y_SET_SELECTION, NULL, be, be, 0));
  }
  if (!strcmp(member, "GetStringAtOffset") ||
      !strcmp(member, "GetTextAtOffset")) {
    dbus_int32_t off = 0, s = 0, e = 0;
    dbus_uint32_t gran = 0;
    char *slice;
    const char *sv;
    DBusMessage *reply;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &off,
                               DBUS_TYPE_UINT32, &gran, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected offset, granularity");
    if (node && nchars > 0) {
      if (off < 0)
        off = 0;
      if (off >= nchars)
        off = nchars - 1;
      if (gran == 0) {
        s = off;
        e = off + 1;
      } else {
        s = 0;
        e = nchars;
      }
    }
    slice = a11y_slice_chars(txt, s, e);
    sv = slice ? slice : "";
    reply = dbus_message_new_method_return(msg);
    if (reply)
      (void)dbus_message_append_args(reply, DBUS_TYPE_STRING, &sv,
                                     DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32, &e,
                                     DBUS_TYPE_INVALID);
    free(slice);
    return reply;
  }
  if (!strcmp(member, "GetCharacterExtents") ||
      !strcmp(member, "GetRangeExtents")) {
    dbus_int32_t a = 0, b = 0, bx, by, bw, bh;
    dbus_uint32_t ct = 0;
    DBusMessage *reply;
    if (!strcmp(member, "GetCharacterExtents")) {
      if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &a,
                                 DBUS_TYPE_UINT32, &ct, DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Expected offset, coord_type");
    } else {
      if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &a,
                                 DBUS_TYPE_INT32, &b, DBUS_TYPE_UINT32, &ct,
                                 DBUS_TYPE_INVALID))
        return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                      "Expected start, end, coord_type");
    }
    a11y_extents(0, node, ct, &bx, &by, &bw, &bh);
    reply = dbus_message_new_method_return(msg);
    if (reply)
      (void)dbus_message_append_args(reply, DBUS_TYPE_INT32, &bx,
                                     DBUS_TYPE_INT32, &by, DBUS_TYPE_INT32, &bw,
                                     DBUS_TYPE_INT32, &bh, DBUS_TYPE_INVALID);
    return reply;
  }
  return NULL;
}

static int a11y_splice_edit(const SzA11yNode *node, dbus_int32_t start_c,
                            dbus_int32_t end_c, const char *ins,
                            size_t ins_bytes) {
  const char *cur;
  dbus_int32_t nchars;
  size_t cur_len, b0, b1, new_len;
  char *buf;
  int caret;
  if (!node || !node->editable || !(node->actions & SZ_A11Y_SET_TEXT))
    return 0;
  if (q_free_slots() < 2)
    return 0;
  cur = a11y_node_text(node);
  cur_len = strlen(cur);
  nchars = utf8_char_count(cur);
  if (start_c < 0)
    start_c = (start_c == -1) ? nchars : 0;
  if (end_c < 0 || end_c > nchars)
    end_c = nchars;
  if (start_c > nchars)
    start_c = nchars;
  if (start_c > end_c)
    start_c = end_c;
  b0 = utf8_char_to_byte(cur, start_c);
  b1 = utf8_char_to_byte(cur, end_c);
  if (ins_bytes > (size_t)INT_MAX - cur_len)
    return 0;
  new_len = b0 + ins_bytes + (cur_len - b1);
  buf = (char *)malloc(new_len + 1);
  if (!buf)
    return 0;
  if (b0 > 0)
    memcpy(buf, cur, b0);
  if (ins_bytes > 0 && ins)
    memcpy(buf + b0, ins, ins_bytes);
  if (cur_len > b1)
    memcpy(buf + b0 + ins_bytes, cur + b1, cur_len - b1);
  buf[new_len] = '\0';
  caret = (int)(b0 + ins_bytes);
  (void)a11y_enqueue(node->id, SZ_A11Y_SET_TEXT, buf, 0, 0, 0);
  (void)a11y_enqueue(node->id, SZ_A11Y_SET_SELECTION, NULL, caret, caret, 0);
  free(buf);
  return 1;
}

static DBusMessage *a11y_handle_editable(DBusMessage *msg,
                                         const SzA11yNode *node) {
  const char *member = dbus_message_get_member(msg);
  if (!member)
    return NULL;
  if (!strcmp(member, "SetTextContents")) {
    const char *contents = NULL;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &contents,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected string");
    if (!node || !node->editable || !(node->actions & SZ_A11Y_SET_TEXT))
      return a11y_reply_bool(msg, 0);
    return a11y_reply_bool(
        msg, a11y_enqueue(node->id, SZ_A11Y_SET_TEXT, contents, 0, 0, 0));
  }
  if (!strcmp(member, "InsertText")) {
    dbus_int32_t pos = 0, len = -1;
    const char *ins = NULL;
    size_t ins_bytes;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &pos,
                               DBUS_TYPE_STRING, &ins, DBUS_TYPE_INT32, &len,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected position, text, length");
    ins_bytes = (len < 0) ? strlen(ins) : utf8_char_to_byte(ins, len);
    return a11y_reply_bool(msg,
                           a11y_splice_edit(node, pos, pos, ins, ins_bytes));
  }
  if (!strcmp(member, "DeleteText")) {
    dbus_int32_t s = 0, e = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32,
                               &e, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected start, end");
    return a11y_reply_bool(msg, a11y_splice_edit(node, s, e, "", 0));
  }
  if (!strcmp(member, "CopyText")) {
    dbus_int32_t s = 0, e = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32,
                               &e, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected start, end");
    if (node && node->editable) {
      char *slice = a11y_slice_chars(a11y_node_text(node), s, e);
      if (slice) {
        (void)sz_embedder_clipboard_set(slice);
        free(slice);
      }
    }
    return dbus_message_new_method_return(msg);
  }
  if (!strcmp(member, "CutText")) {
    dbus_int32_t s = 0, e = 0;
    int ok = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &s, DBUS_TYPE_INT32,
                               &e, DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected start, end");
    if (node && node->editable) {
      char *slice = a11y_slice_chars(a11y_node_text(node), s, e);
      if (slice) {
        (void)sz_embedder_clipboard_set(slice);
        free(slice);
        ok = a11y_splice_edit(node, s, e, "", 0);
      }
    }
    return a11y_reply_bool(msg, ok);
  }
  if (!strcmp(member, "PasteText")) {
    dbus_int32_t pos = 0;
    int ok = 0;
    if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_INT32, &pos,
                               DBUS_TYPE_INVALID))
      return dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS,
                                    "Expected position");
    if (node && node->editable) {
      char *clip = sz_embedder_clipboard_get();
      const char *paste = clip ? clip : "";
      ok = a11y_splice_edit(node, pos, pos, paste, strlen(paste));
      free(clip);
    }
    return a11y_reply_bool(msg, ok);
  }
  return NULL;
}

static int a11y_append_cache_item(DBusMessageIter *arr, int is_root,
                                  const SzA11yNode *node, uintptr_t self_id) {
  DBusMessageIter st;
  char self_path[96], parent_path[96];
  const char *parent_bus = "";
  const char *name, *desc, *rname = NULL;
  dbus_uint32_t role = 0;
  dbus_int32_t idx, ccount;
  a11y_make_path(is_root ? 0 : self_id, self_path, sizeof(self_path));
  a11y_parent_ref(is_root, node, &parent_bus, parent_path, sizeof(parent_path));
  idx = a11y_index_in_parent(is_root, node);
  ccount = a11y_child_count(is_root ? 0 : self_id);
  name = is_root ? (g_win_title ? g_win_title : "Scuzz Lang")
                 : (node && node->label ? node->label : "");
  desc = (!is_root && node && node->description) ? node->description : "";
  a11y_role_info(is_root, node, &role, &rname);
  if (!dbus_message_iter_open_container(arr, DBUS_TYPE_STRUCT, NULL, &st))
    return 0;
  if (!a11y_append_ref(&st, a11y_self_bus(), self_path) ||
      !a11y_append_ref(&st, a11y_self_bus(), ATSPI_ROOT_PATH) ||
      !a11y_append_ref(&st, parent_bus, parent_path) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &idx) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_INT32, &ccount) ||
      !a11y_append_interfaces(&st, is_root, node) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &name) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_UINT32, &role) ||
      !dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &desc) ||
      !a11y_append_states(&st, is_root, node)) {
    dbus_message_iter_close_container(arr, &st);
    return 0;
  }
  return dbus_message_iter_close_container(arr, &st);
}

static DBusMessage *a11y_handle_cache(DBusMessage *msg) {
  const char *member = dbus_message_get_member(msg);
  DBusMessage *reply;
  DBusMessageIter out, arr;
  if (!member || strcmp(member, "GetItems") != 0)
    return NULL;
  reply = dbus_message_new_method_return(msg);
  if (!reply)
    return NULL;
  dbus_message_iter_init_append(reply, &out);
  if (!dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY,
                                        "((so)(so)(so)iiassusau)", &arr)) {
    dbus_message_unref(reply);
    return NULL;
  }
  (void)a11y_append_cache_item(&arr, 1, NULL, 0);
  if (g_a11y_snapshot) {
    size_t i, n = sz_a11y_snapshot_count(g_a11y_snapshot);
    for (i = 0; i < n; i++) {
      const SzA11yNode *node = sz_a11y_snapshot_node(g_a11y_snapshot, i);
      if (node)
        (void)a11y_append_cache_item(&arr, 0, node, node->id);
    }
  }
  dbus_message_iter_close_container(&out, &arr);
  return reply;
}

static DBusHandlerResult a11y_dispatch_path(DBusConnection *conn,
                                            DBusMessage *msg, void *user_data) {
  const char *path = dbus_message_get_path(msg);
  const char *iface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);
  uintptr_t self_id = 0;
  int is_root = 0;
  const SzA11yNode *node = NULL;
  DBusMessage *reply = NULL;
  (void)user_data;
  if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL || !path ||
      !member)
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  if (iface && !strcmp(iface, DBUS_INTERFACE_INTROSPECTABLE) &&
      !strcmp(member, "Introspect")) {
    static const char k_xml[] =
        "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
        "Introspection 1.0//EN\" "
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
        "<node>"
        "<interface name=\"org.freedesktop.DBus.Properties\"/>"
        "<interface name=\"org.a11y.atspi.Accessible\"/>"
        "<interface name=\"org.a11y.atspi.Component\"/>"
        "<interface name=\"org.a11y.atspi.Action\"/>"
        "<interface name=\"org.a11y.atspi.Value\"/>"
        "<interface name=\"org.a11y.atspi.Text\"/>"
        "<interface name=\"org.a11y.atspi.EditableText\"/>"
        "</node>";
    reply = a11y_reply_str(msg, k_xml);
    if (reply) {
      (void)dbus_connection_send(conn, reply, NULL);
      dbus_message_unref(reply);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
  }
  if (!strcmp(path, ATSPI_CACHE_PATH)) {
    if (!iface || !strcmp(iface, ATSPI_IFACE_CACHE))
      reply = a11y_handle_cache(msg);
    if (!reply)
      reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, member);
    if (reply) {
      (void)dbus_connection_send(conn, reply, NULL);
      dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  if (!a11y_parse_path(path, &self_id)) {
    reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_OBJECT, path);
    if (reply) {
      (void)dbus_connection_send(conn, reply, NULL);
      dbus_message_unref(reply);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  is_root = (self_id == 0);
  if (!is_root)
    node = a11y_find_node(g_a11y_snapshot, self_id);

  if (iface && !strcmp(iface, DBUS_INTERFACE_PROPERTIES))
    reply = a11y_handle_properties(msg, is_root, node, self_id);
  else if (!iface || !strcmp(iface, ATSPI_IFACE_ACCESSIBLE))
    reply = a11y_handle_accessible(msg, is_root, node, self_id);
  if (!reply && (!iface || !strcmp(iface, ATSPI_IFACE_COMPONENT)))
    reply = a11y_handle_component(msg, is_root, node, self_id);
  if (!reply && !is_root && (!iface || !strcmp(iface, ATSPI_IFACE_ACTION)))
    reply = a11y_handle_action(msg, node);
  if (!reply && !is_root && (!iface || !strcmp(iface, ATSPI_IFACE_TEXT)))
    reply = a11y_handle_text(msg, node);
  if (!reply && !is_root && (!iface || !strcmp(iface, ATSPI_IFACE_EDITABLE)))
    reply = a11y_handle_editable(msg, node);
  if (!reply)
    reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, member);
  if (reply) {
    (void)dbus_connection_send(conn, reply, NULL);
    dbus_message_unref(reply);
  }
  return DBUS_HANDLER_RESULT_HANDLED;
}

static void a11y_emit_object_event(uintptr_t id, const char *member,
                                   const char *detail, dbus_int32_t d1,
                                   dbus_int32_t d2) {
  char path[96];
  DBusMessage *sig;
  DBusMessageIter out, var, props;
  const char *det = detail ? detail : "";
  dbus_int32_t zero = 0;
  if (!g_a11y_bus || !dbus_connection_get_is_connected(g_a11y_bus))
    return;
  a11y_make_path(id, path, sizeof(path));
  sig = dbus_message_new_signal(path, ATSPI_IFACE_EVENT_OBJECT, member);
  if (!sig)
    return;
  dbus_message_iter_init_append(sig, &out);
  (void)dbus_message_iter_append_basic(&out, DBUS_TYPE_STRING, &det);
  (void)dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &d1);
  (void)dbus_message_iter_append_basic(&out, DBUS_TYPE_INT32, &d2);
  if (dbus_message_iter_open_container(&out, DBUS_TYPE_VARIANT, "i", &var)) {
    (void)dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &zero);
    dbus_message_iter_close_container(&out, &var);
  }
  if (dbus_message_iter_open_container(&out, DBUS_TYPE_ARRAY, "{sv}", &props))
    dbus_message_iter_close_container(&out, &props);
  (void)dbus_connection_send(g_a11y_bus, sig, NULL);
  dbus_message_unref(sig);
}

static void a11y_emit_diff(const SzA11ySnapshot *prev,
                           const SzA11ySnapshot *next) {
  size_t i, np, nn;
  int tree_changed = 0;
  if (!g_a11y_bus || !next)
    return;
  nn = sz_a11y_snapshot_count(next);
  if (!prev) {
    a11y_emit_object_event(0, "ChildrenChanged", "add", 0, 0);
    return;
  }
  np = sz_a11y_snapshot_count(prev);
  if (np != nn)
    tree_changed = 1;
  for (i = 0; i < nn; i++) {
    const SzA11yNode *cur = sz_a11y_snapshot_node(next, i);
    const SzA11yNode *old = cur ? a11y_find_node(prev, cur->id) : NULL;
    if (!cur)
      continue;
    if (!old || old->parent_id != cur->parent_id) {
      tree_changed = 1;
      if (cur->focused)
        a11y_emit_object_event(cur->id, "StateChanged", "focused", 1, 0);
      continue;
    }
    if (old->focused != cur->focused)
      a11y_emit_object_event(cur->id, "StateChanged", "focused",
                             cur->focused ? 1 : 0, 0);
    if (old->checked != cur->checked && cur->checked >= 0)
      a11y_emit_object_event(cur->id, "StateChanged", "checked",
                             cur->checked ? 1 : 0, 0);
    if (old->selected != cur->selected && cur->selected >= 0)
      a11y_emit_object_event(cur->id, "StateChanged", "selected",
                             cur->selected ? 1 : 0, 0);
    if (old->expanded != cur->expanded && cur->expanded >= 0)
      a11y_emit_object_event(cur->id, "StateChanged", "expanded",
                             cur->expanded ? 1 : 0, 0);
    if (a11y_has_value(cur) && old->number != cur->number)
      a11y_emit_object_event(cur->id, "PropertyChange", "accessible-value", 0,
                             0);
    if (cur->editable) {
      const char *ov = old->value ? old->value : "";
      const char *nv = cur->value ? cur->value : "";
      if (strcmp(ov, nv) != 0)
        a11y_emit_object_event(cur->id, "TextChanged", "insert", 0,
                               utf8_char_count(nv));
      if (old->selection_end != cur->selection_end)
        a11y_emit_object_event(
            cur->id, "TextCaretMoved", "",
            utf8_byte_to_char(nv, cur->selection_end), 0);
      if (old->selection_start != cur->selection_start ||
          old->selection_end != cur->selection_end)
        a11y_emit_object_event(cur->id, "TextSelectionChanged", "", 0, 0);
    }
  }
  if (tree_changed)
    a11y_emit_object_event(0, "ChildrenChanged", "", 0, 0);
}

static void a11y_close_bus(void) {
  if (g_a11y_bus) {
    (void)dbus_connection_unregister_object_path(g_a11y_bus, "/org/a11y/atspi");
    dbus_connection_close(g_a11y_bus);
    dbus_connection_unref(g_a11y_bus);
    g_a11y_bus = NULL;
  }
  free(g_a11y_plug_bus);
  g_a11y_plug_bus = NULL;
  free(g_a11y_plug_path);
  g_a11y_plug_path = NULL;
}

static void a11y_shutdown(void) {
  a11y_close_bus();
  if (g_a11y_snapshot) {
    sz_a11y_snapshot_free(g_a11y_snapshot);
    g_a11y_snapshot = NULL;
  }
  g_a11y_w = 0;
  g_a11y_h = 0;
  g_a11y_probed = 0;
  g_a11y_app_id = 0;
}

static char *a11y_lookup_session_bus_addr(void) {
  const char *saddr = getenv("DBUS_SESSION_BUS_ADDRESS");
  DBusConnection *sconn;
  DBusMessage *msg, *reply;
  const char *got = NULL;
  char *res = NULL;
  if (!saddr || !saddr[0])
    return NULL;
  sconn = dbus_connection_open_private(saddr, NULL);
  if (!sconn)
    return NULL;
  dbus_connection_set_exit_on_disconnect(sconn, FALSE);
  if (!dbus_bus_register(sconn, NULL)) {
    dbus_connection_close(sconn);
    dbus_connection_unref(sconn);
    return NULL;
  }
  msg = dbus_message_new_method_call(ATSPI_BUS_SERVICE, ATSPI_BUS_PATH,
                                     ATSPI_BUS_IFACE, "GetAddress");
  if (msg) {
    /* Do not start a persistent accessibility daemon on headless hosts. */
    dbus_message_set_auto_start(msg, FALSE);
    reply = dbus_connection_send_with_reply_and_block(sconn, msg, 250, NULL);
    dbus_message_unref(msg);
    if (reply) {
      if (dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &got,
                                DBUS_TYPE_INVALID) &&
          got && got[0])
        res = clip_dup(got);
      dbus_message_unref(reply);
    }
  }
  dbus_connection_close(sconn);
  dbus_connection_unref(sconn);
  return res;
}

static void a11y_embed_root(void) {
  DBusMessage *msg, *reply;
  DBusMessageIter args, riter, rst;
  const char *self_bus = a11y_self_bus();
  if (!g_a11y_bus || !self_bus[0])
    return;
  msg = dbus_message_new_method_call(ATSPI_REGISTRY_SERVICE, ATSPI_ROOT_PATH,
                                     ATSPI_SOCKET_IFACE, "Embed");
  if (!msg)
    return;
  dbus_message_set_auto_start(msg, FALSE);
  dbus_message_iter_init_append(msg, &args);
  if (!a11y_append_ref(&args, self_bus, ATSPI_ROOT_PATH)) {
    dbus_message_unref(msg);
    return;
  }
  reply = dbus_connection_send_with_reply_and_block(g_a11y_bus, msg, 250, NULL);
  dbus_message_unref(msg);
  if (!reply)
    return;
  if (dbus_message_iter_init(reply, &riter) &&
      dbus_message_iter_get_arg_type(&riter) == DBUS_TYPE_STRUCT) {
    const char *pbus = NULL, *ppath = NULL;
    dbus_message_iter_recurse(&riter, &rst);
    if (dbus_message_iter_get_arg_type(&rst) == DBUS_TYPE_STRING) {
      dbus_message_iter_get_basic(&rst, &pbus);
      if (dbus_message_iter_next(&rst) &&
          dbus_message_iter_get_arg_type(&rst) == DBUS_TYPE_OBJECT_PATH) {
        dbus_message_iter_get_basic(&rst, &ppath);
        if (ppath && ppath[0] == '/') {
          free(g_a11y_plug_bus);
          free(g_a11y_plug_path);
          g_a11y_plug_bus = clip_dup(pbus ? pbus : "");
          g_a11y_plug_path = clip_dup(ppath);
        }
      }
    }
  }
  dbus_message_unref(reply);
}

static void a11y_poll(void) {
  int pass;
  if (!g_a11y_bus)
    return;
  if (!dbus_connection_get_is_connected(g_a11y_bus)) {
    a11y_close_bus();
    return;
  }
  for (pass = 0; pass < 16; pass++) {
    int dispatched = 0;
    (void)dbus_connection_read_write(g_a11y_bus, 0);
    while (dbus_connection_dispatch(g_a11y_bus) == DBUS_DISPATCH_DATA_REMAINS)
      dispatched = 1;
    dbus_connection_flush(g_a11y_bus);
    if (!dispatched)
      break;
  }
}

int sz_embedder_accessibility_available(void) {
  static const DBusObjectPathVTable k_vtable = {NULL, a11y_dispatch_path, NULL,
                                                NULL, NULL, NULL};
  const char *env_addr;
  char *bus_addr = NULL;
  DBusConnection *conn;
  if (g_user_quit)
    return 0;
  if (g_a11y_bus) {
    if (dbus_connection_get_is_connected(g_a11y_bus))
      return 1;
    a11y_close_bus();
  }
  if (g_a11y_probed)
    return 0;
  g_a11y_probed = 1;
  env_addr = getenv("AT_SPI_BUS_ADDRESS");
  if (env_addr && (!strcmp(env_addr, "none") || !strcmp(env_addr, "disabled")))
    return 0;
  if (env_addr && env_addr[0])
    bus_addr = clip_dup(env_addr);
  else
    bus_addr = a11y_lookup_session_bus_addr();
  if (!bus_addr || !bus_addr[0]) {
    free(bus_addr);
    return 0;
  }
  conn = dbus_connection_open_private(bus_addr, NULL);
  free(bus_addr);
  if (!conn)
    return 0;
  dbus_connection_set_exit_on_disconnect(conn, FALSE);
  if (!dbus_bus_register(conn, NULL) ||
      !dbus_connection_register_fallback(conn, "/org/a11y/atspi", &k_vtable,
                                         NULL)) {
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return 0;
  }
  g_a11y_bus = conn;
  a11y_embed_root();
  dbus_connection_flush(g_a11y_bus);
  return 1;
}

void sz_embedder_publish_accessibility(SzA11ySnapshot *snapshot, int width,
                                       int height) {
  SzA11ySnapshot *prev = g_a11y_snapshot;
  g_a11y_snapshot = snapshot;
  g_a11y_w = width;
  g_a11y_h = height;
  if (g_a11y_bus && dbus_connection_get_is_connected(g_a11y_bus)) {
    a11y_emit_diff(prev, snapshot);
    a11y_poll();
  }
  if (prev)
    sz_a11y_snapshot_free(prev);
}

static int resize_queued(void) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP)
    if (g_queue[i].kind == SZ_INPUT_RESIZE)
      return 1;
  return 0;
}

static void enqueue_resize(int width, int height) {
  SzInputEvent ev;
  /* Coalesce consecutive resizes. A WM drag storm must not fill the
   * queue and starve key events. */
  if (g_q_head != g_q_tail) {
    SzInputEvent *last = &g_queue[(g_q_tail + EVENT_CAP - 1) % EVENT_CAP];
    if (last->kind == SZ_INPUT_RESIZE) {
      last->width = width;
      last->height = height;
      return;
    }
  }
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_RESIZE;
  ev.width = width;
  ev.height = height;
  ev.scale = 0; /* X11 blits 1:1; keep the session scale */
  q_push(&ev);
}

static void x11_drain_pending(void) {
  if (!g_dpy || g_user_quit)
    return;
  while (XPending(g_dpy)) {
    XEvent ev;
    XNextEvent(g_dpy, &ev);
    if (x11_dispatch_event(&ev))
      return;
  }
}

int sz_embedder_poll_event(SzInputEvent *out) {
  if (!out)
    return 0;
  int previous = g_motion_reduced;
  motion_poll();
  if (out && previous != g_motion_reduced) {
    *out = (SzInputEvent){.kind = SZ_INPUT_REDUCED_MOTION,
                          .reduced_motion = g_motion_reduced};
    return 1;
  }
  a11y_poll();
  x11_drain_pending();
  if (!out || g_q_head == g_q_tail)
    return 0;
  *out = g_queue[g_q_head];
  if (g_a11y_text_q[g_q_head]) {
    free(g_a11y_poll_text);
    g_a11y_poll_text = g_a11y_text_q[g_q_head];
    g_a11y_text_q[g_q_head] = NULL;
    out->text = g_a11y_poll_text;
  }
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  return 1;
}

static void enqueue_pointer(SzPointerPhase phase, float x, float y, int button) {
  SzInputEvent ev;
  /* Coalesce consecutive moves. A motion storm must not fill the queue
   * and starve key events. */
  if (phase == SZ_POINTER_MOVE && g_q_head != g_q_tail) {
    SzInputEvent *last = &g_queue[(g_q_tail + EVENT_CAP - 1) % EVENT_CAP];
    if (last->kind == SZ_INPUT_POINTER &&
        last->pointer_phase == SZ_POINTER_MOVE &&
        last->pointer_button == button) {
      last->x = x;
      last->y = y;
      return;
    }
  }
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
  ev.pointer_button = button;
  ev.x = x;
  ev.y = y;
  q_push(&ev);
}

static void enqueue_scroll(float x, float y, float dx, float dy) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = x;
  ev.y = y;
  ev.dx = dx;
  ev.dy = dy;
  q_push(&ev);
}

static void stash_text_only(const char *text, const char **out) {
  size_t nt;
  char *tdst;
  if (!out)
    return;
  if (!text)
    text = "";
  nt = strlen(text);
  if (nt >= TEXT_LEN)
    nt = TEXT_LEN - 1;
  tdst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  memcpy(tdst, text, nt);
  tdst[nt] = '\0';
  *out = tdst;
}

static int __attribute__((unused)) enqueue_compose(const char *text) {
  SzInputEvent ev;
  const char *t;
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i]))
    return 0;
  stash_text_only(text, &t);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_COMPOSE;
  ev.text = t;
  return q_push(&ev);
}

static int __attribute__((unused)) enqueue_text_edit(const char *text) {
  SzInputEvent ev;
  const char *t;
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i]))
    return 0;
  stash_text_only(text ? text : "", &t);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = t;
  return q_push(&ev);
}

#if SCUZZ_HAVE_XIM
static void xim_preedit_draw(XIM xim, XPointer client_data, XPointer call_data) {
  XIMPreeditDrawCallbackStruct *pre;
  (void)xim;
  (void)client_data;
  if (!call_data)
    return;
  pre = (XIMPreeditDrawCallbackStruct *)call_data;
  if (pre->text && pre->text->string.multi_byte)
    enqueue_compose(pre->text->string.multi_byte);
  else if (pre->chg_length == 0)
    enqueue_compose("");
}

static void xim_preedit_done(XIM xim, XPointer client_data, XPointer call_data) {
  (void)xim;
  (void)client_data;
  (void)call_data;
  enqueue_compose("");
}

static void x11_xim_shutdown(void) {
  if (g_xic) {
    XDestroyIC(g_xic);
    g_xic = NULL;
  }
  if (g_xim) {
    XCloseIM(g_xim);
    g_xim = NULL;
  }
}

static void x11_xim_open(void) {
  XIMCallback draw_cb;
  XIMCallback done_cb;
  XVaNestedList preedit;
  if (!g_dpy || g_xim)
    return;
  g_xim = XOpenIM(g_dpy, NULL, NULL, NULL);
  if (!g_xim)
    return;
  draw_cb.callback = (XIMProc)xim_preedit_draw;
  draw_cb.client_data = NULL;
  done_cb.callback = (XIMProc)xim_preedit_done;
  done_cb.client_data = NULL;
  preedit = XVaCreateNestedList(0, XNPreeditDrawCallback, &draw_cb,
                                XNPreeditDoneCallback, &done_cb, NULL);
  g_xic = XCreateIC(g_xim, XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing,
                    XNClientWindow, g_win, XNFocusWindow, g_win,
                    XNPreeditAttributes, preedit, NULL);
  XFree(preedit);
  if (!g_xic) {
    XCloseIM(g_xim);
    g_xim = NULL;
  }
}
#else
static void x11_xim_shutdown(void) {}
static void x11_xim_open(void) {}
#endif

static void enqueue_key(const char *name, const char *text, int mods, int repeat) {
  SzInputEvent ev;
  const char *k;
  const char *t;
  if (q_full() || text_slot_queued(g_key_bufs[g_text_i]) ||
      text_slot_queued(g_text_bufs[g_text_i]))
    return;
  stash_key_text(name, text, &k, &t);
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_KEY;
  ev.key = k;
  ev.text = t;
  ev.key_mods = mods;
  ev.key_repeat = repeat ? 1 : 0;
  q_push(&ev);
}

static int x11_is_modifier(KeySym ks) {
  return ks == XK_Shift_L || ks == XK_Shift_R || ks == XK_Control_L ||
         ks == XK_Control_R || ks == XK_Alt_L || ks == XK_Alt_R ||
         ks == XK_Meta_L || ks == XK_Meta_R || ks == XK_Super_L ||
         ks == XK_Super_R || ks == XK_Hyper_L || ks == XK_Hyper_R ||
         ks == XK_Caps_Lock || ks == XK_Num_Lock || ks == XK_Scroll_Lock ||
         ks == XK_Mode_switch || ks == XK_ISO_Level3_Shift;
}

static int x11_key_no_insert(const char *name) {
  return strcmp(name, "Backspace") == 0 || strcmp(name, "Enter") == 0 ||
         strcmp(name, "Tab") == 0 || strcmp(name, "Escape") == 0 ||
         strcmp(name, "Delete") == 0 || strncmp(name, "Arrow", 5) == 0 ||
         strcmp(name, "Home") == 0 || strcmp(name, "End") == 0 ||
         strcmp(name, "PageUp") == 0 || strcmp(name, "PageDown") == 0;
}

static void x11_key_name(KeySym ks, char *out, size_t cap) {
  const char *s = NULL;
  const char *xs;
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  switch (ks) {
  case XK_Return:
  case XK_KP_Enter:
    s = "Enter";
    break;
  case XK_Tab:
  case XK_ISO_Left_Tab:
    s = "Tab";
    break;
  case XK_BackSpace:
    s = "Backspace";
    break;
  case XK_Delete:
  case XK_KP_Delete:
    s = "Delete";
    break;
  case XK_Escape:
    s = "Escape";
    break;
  case XK_Left:
  case XK_KP_Left:
    s = "ArrowLeft";
    break;
  case XK_Right:
  case XK_KP_Right:
    s = "ArrowRight";
    break;
  case XK_Up:
  case XK_KP_Up:
    s = "ArrowUp";
    break;
  case XK_Down:
  case XK_KP_Down:
    s = "ArrowDown";
    break;
  case XK_Home:
  case XK_KP_Home:
    s = "Home";
    break;
  case XK_End:
  case XK_KP_End:
    s = "End";
    break;
  case XK_Page_Up:
  case XK_KP_Page_Up:
    s = "PageUp";
    break;
  case XK_Page_Down:
  case XK_KP_Page_Down:
    s = "PageDown";
    break;
  case XK_space:
  case XK_KP_Space:
    s = "Space";
    break;
  default:
    break;
  }
  if (s) {
    snprintf(out, cap, "%s", s);
    return;
  }
  if (ks >= XK_A && ks <= XK_Z) {
    out[0] = (char)('a' + (int)(ks - XK_A));
    out[1] = '\0';
    return;
  }
  if (ks >= XK_a && ks <= XK_z) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  if (ks >= XK_0 && ks <= XK_9) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  if (ks >= 0x20 && ks <= 0x7e) {
    out[0] = (char)ks;
    out[1] = '\0';
    return;
  }
  xs = XKeysymToString(ks);
  if (xs && xs[0]) {
    snprintf(out, cap, "%s", xs);
    return;
  }
  snprintf(out, cap, "Unidentified");
}

static void latin1_to_utf8(const char *in, int n, char *out, size_t cap) {
  size_t o = 0;
  int i;
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  if (!in || n <= 0)
    return;
  for (i = 0; i < n && o + 3 < cap; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c < 0x80)
      out[o++] = (char)c;
    else {
      out[o++] = (char)(0xc0 | (c >> 6));
      out[o++] = (char)(0x80 | (c & 0x3f));
    }
  }
  out[o] = '\0';
}

static int frame_bytes(int width, int height, size_t *out) {
  size_t w;
  size_t h;
  if (width <= 0 || height <= 0 || !out)
    return 0;
  /* X11 window geometry is CARD16 on the wire. Bigger sizes truncate in
   * the protocol and desync the blit from g_w/g_h. Reject them. */
  if (width > 65535 || height > 65535)
    return 0;
  w = (size_t)width;
  h = (size_t)height;
  if (w > SIZE_MAX / 4)
    return 0;
  if (h > SIZE_MAX / (w * 4))
    return 0;
  *out = w * h * 4;
  return 1;
}

/* Put an 8-bit channel into a TrueColor mask. */
static uint32_t place_chan(uint8_t v, unsigned long mask) {
  unsigned long m;
  unsigned shift;
  unsigned bits;
  if (!mask)
    return 0;
  m = mask;
  shift = 0;
  while ((m & 1UL) == 0UL) {
    m >>= 1;
    shift++;
  }
  bits = 0;
  while (m & 1UL) {
    m >>= 1;
    bits++;
  }
  if (bits >= 8)
    return ((uint32_t)v << (shift + bits - 8)) & (uint32_t)mask;
  return (((uint32_t)v >> (8 - bits)) << shift) & (uint32_t)mask;
}

/* Backing image for the frame size. Reallocates when the size changes;
 * the window itself is created once and follows with XResizeWindow. */
static int ensure_frame(int width, int height) {
  size_t need;
  char *data;
  XImage *img;
  Visual *vis;
  int depth;
  int screen;
  if (g_img && g_w == width && g_h == height)
    return 1;
  if (!frame_bytes(width, height, &need))
    return 0;
  data = (char *)malloc(need);
  if (!data)
    return 0;
  screen = DefaultScreen(g_dpy);
  vis = DefaultVisual(g_dpy, screen);
  depth = DefaultDepth(g_dpy, screen);
  img = XCreateImage(g_dpy, vis, (unsigned)depth, ZPixmap, 0, data,
                     (unsigned)width, (unsigned)height, 32, width * 4);
  if (!img) {
    free(data);
    return 0;
  }
  if (img->bits_per_pixel != 32) {
    /* Both blit paths write one uint32_t per pixel. A 16-bit display
     * would get garbage. Fail loudly instead. */
    fprintf(stderr, "scuzz embedder: need 32 bits/pixel, got %d\n",
            img->bits_per_pixel);
    XDestroyImage(img); /* frees data */
    return 0;
  }
  if (g_img) {
    /* XDestroyImage frees g_img_data */
    g_img->data = g_img_data;
    XDestroyImage(g_img);
  } else {
    free(g_img_data);
  }
  g_img = img;
  g_img_data = data;
  g_w = width;
  g_h = height;
  return 1;
}

/* Create the window once. Size changes after creation swap the backing
 * image (ensure_frame) and, when the app drove the change, resize the
 * window from present. */
static int ensure_window(const char *title, int width, int height) {
  int screen;

  if (g_ready)
    return 1;

  g_dpy = XOpenDisplay(NULL);
  if (!g_dpy) {
    fprintf(stderr, "scuzz embedder: cannot open DISPLAY\n");
    return 0;
  }
  XSetErrorHandler(x11_on_error);

  screen = DefaultScreen(g_dpy);
  g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0,
                              (unsigned)width, (unsigned)height, 1,
                              BlackPixel(g_dpy, screen),
                              WhitePixel(g_dpy, screen));
  XStoreName(g_dpy, g_win, title ? title : "Scuzz Lang");
  g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(g_dpy, g_win, &g_wm_delete, 1);
  {
    /* A floor only. A locked min == max would disable WM resize and the
     * maximize control. */
    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize;
    hints.min_width = 64;
    hints.min_height = 64;
    XSetWMNormalHints(g_dpy, g_win, &hints);
  }
  XSelectInput(g_dpy, g_win,
               ExposureMask | StructureNotifyMask | KeyPressMask |
                   KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                   PointerMotionMask | Button1MotionMask);
  g_win_w = width;
  g_win_h = height;
  XMapWindow(g_dpy, g_win);
  g_gc = DefaultGC(g_dpy, screen);

  if (!ensure_frame(width, height)) {
    sz_embedder_shutdown();
    return 0;
  }

  /* Wait for MapNotify so the first frame is visible. Bound the wait:
   * a WM that never maps must not hang startup. */
  {
    int mapped = 0;
    int i;
    for (i = 0; i < 500 && !mapped; i++) {
      while (XPending(g_dpy)) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        if (ev.type == MapNotify) {
          mapped = 1;
          break;
        }
        /* Dispatch, do not drop: input, close requests, and selection
         * requests that land during the map wait are still real events. */
        if (x11_dispatch_event(&ev)) {
          /* Window died; shutdown already ran. */
          return 0;
        }
      }
      if (!mapped) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 2000000L; /* 2 ms */
        nanosleep(&ts, NULL);
      }
    }
    /* Unmapped after 1 s: present anyway. The blit queues in the server. */
  }

  x11_xim_open();
  g_ready = 1;
  if (g_clip) {
    /* The clipboard can be set before the window exists. Own the
     * selection now that there is a window to serve it. */
    x11_clip_atoms();
    XSetSelectionOwner(g_dpy, g_atom_clipboard, g_win, CurrentTime);
    g_clip_own = XGetSelectionOwner(g_dpy, g_atom_clipboard) == g_win;
  }
  fprintf(stderr, "scuzz embedder: X11 window %dx%d\n", width, height);
  return 1;
}

static void x11_handle_key_release(XKeyEvent *key) {
  if (!key)
    return;
  if (XEventsQueued(g_dpy, QueuedAfterReading) > 0) {
    XEvent nev;
    XPeekEvent(g_dpy, &nev);
    if (nev.type == KeyPress && nev.xkey.keycode == key->keycode &&
        nev.xkey.time == key->time) {
      g_key_repeat_pending = 1;
      return;
    }
  }
  if (g_key_held && key->keycode == g_held_keycode)
    g_key_held = 0;
}

static void x11_handle_key_press(XKeyEvent *key) {
  KeySym unshifted;
  KeySym keysym;
  Status xstatus = XLookupBoth;
  char buf[128];
  char name[KEY_NAME_LEN];
  char utf8[TEXT_LEN];
  int nkey = 0;
  int mods = 0;
  int repeat = g_key_repeat_pending;
  if (!key)
    return;
  g_key_repeat_pending = 0;
  if (g_key_held && key->keycode == g_held_keycode)
    repeat = 1;
  g_held_keycode = key->keycode;
  g_key_held = 1;
  unshifted = XLookupKeysym(key, 0);
  if (x11_is_modifier(unshifted))
    return;
  buf[0] = '\0';
#if SCUZZ_HAVE_XIM
  if (g_xic)
    nkey = Xutf8LookupString(g_xic, key, buf, (int)sizeof(buf) - 1, &keysym,
                             &xstatus);
  else
    nkey = XLookupString(key, buf, (int)sizeof(buf) - 1, &keysym, NULL);
#else
  nkey = XLookupString(key, buf, (int)sizeof(buf) - 1, &keysym, NULL);
  (void)xstatus;
#endif
  if (nkey < 0)
    nkey = 0;
  buf[nkey] = '\0';
  x11_key_name(unshifted, name, sizeof name);
  utf8[0] = '\0';
#if SCUZZ_HAVE_XIM
  if (xstatus == XLookupChars || xstatus == XLookupBoth) {
    if (buf[0]) {
      snprintf(utf8, sizeof utf8, "%s", buf);
      enqueue_text_edit(utf8);
      return;
    }
  }
#endif
  if (!x11_key_no_insert(name)) {
    latin1_to_utf8(buf, nkey, utf8, sizeof utf8);
    if (utf8[0] && (unsigned char)utf8[0] < 32)
      utf8[0] = '\0';
  }
  if (key->state & ShiftMask)
    mods |= SZ_KEY_SHIFT;
  if (key->state & ControlMask)
    mods |= SZ_KEY_CTRL;
  if (key->state & Mod1Mask)
    mods |= SZ_KEY_ALT;
  if (key->state & Mod4Mask)
    mods |= SZ_KEY_CMD;
  enqueue_key(name, utf8, mods, repeat);
}

static int x11_dispatch_event(XEvent *ev) {
  if (!ev || !g_dpy)
    return 0;
  /* Give the input method first claim on key events. */
  if (XFilterEvent(ev, None))
    return 0;
  if (ev->type == DestroyNotify) {
    g_user_quit = 1;
    /* The window is already gone. Skip XDestroyWindow in shutdown.
     * The async BadWindow error would hit the default X error handler
     * and exit the process. */
    g_win = 0;
    sz_embedder_shutdown();
    return 1;
  }
  if (ev->type == ClientMessage &&
      (Atom)ev->xclient.data.l[0] == g_wm_delete) {
    g_user_quit = 1;
    sz_embedder_shutdown();
    return 1;
  }
  if (ev->type == SelectionRequest)
    x11_serve_selection(&ev->xselectionrequest);
  else if (ev->type == ConfigureNotify) {
    /* WM resize (drag, maximize, tile). Tell the session; the next pump
     * lays out and paints at the new size. */
    if (ev->xconfigure.width != g_win_w ||
        ev->xconfigure.height != g_win_h) {
      g_win_w = ev->xconfigure.width;
      g_win_h = ev->xconfigure.height;
      enqueue_resize(g_win_w, g_win_h);
    }
  } else if (ev->type == Expose && ev->xexpose.count == 0 && g_ready && g_img)
    /* Re-blit the last frame. Un-cover and de-iconify must repaint even
     * when the session is idle. */
    XPutImage(g_dpy, g_win, g_gc, g_img, 0, 0, 0, 0, (unsigned)g_w,
              (unsigned)g_h);
  else if (ev->type == SelectionClear)
    g_clip_own = 0;
  else if (ev->type == ButtonPress && ev->xbutton.button == 1)
    enqueue_pointer(SZ_POINTER_DOWN, (float)ev->xbutton.x, (float)ev->xbutton.y,
                    1);
  else if (ev->type == ButtonPress && ev->xbutton.button == 3)
    enqueue_pointer(SZ_POINTER_DOWN, (float)ev->xbutton.x, (float)ev->xbutton.y,
                    3);
  else if (ev->type == MotionNotify) {
    int button = 0;
    if (ev->xmotion.state & Button1Mask)
      button = 1;
    else if (ev->xmotion.state & Button3Mask)
      button = 3;
    enqueue_pointer(SZ_POINTER_MOVE, (float)ev->xmotion.x, (float)ev->xmotion.y,
                    button);
  } else if (ev->type == ButtonRelease && ev->xbutton.button == 1)
    enqueue_pointer(SZ_POINTER_UP, (float)ev->xbutton.x, (float)ev->xbutton.y,
                    1);
  else if (ev->type == ButtonRelease && ev->xbutton.button == 3)
    enqueue_pointer(SZ_POINTER_UP, (float)ev->xbutton.x, (float)ev->xbutton.y,
                    3);
  /* Wheel: 4 = up, 5 = down. Shift or buttons 6/7 pan x. Positive dy = content
   * up. Positive dx = content left (matches SZ_INPUT_SCROLL). */
  else if (ev->type == ButtonPress && ev->xbutton.button == 4) {
    if (ev->xbutton.state & ShiftMask)
      enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, 40.f, 0.f);
    else
      enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, 0.f, 40.f);
  } else if (ev->type == ButtonPress && ev->xbutton.button == 5) {
    if (ev->xbutton.state & ShiftMask)
      enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, -40.f, 0.f);
    else
      enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, 0.f, -40.f);
  } else if (ev->type == ButtonPress && ev->xbutton.button == 6)
    enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, -40.f, 0.f);
  else if (ev->type == ButtonPress && ev->xbutton.button == 7)
    enqueue_scroll((float)ev->xbutton.x, (float)ev->xbutton.y, 40.f, 0.f);
  else if (ev->type == KeyRelease)
    x11_handle_key_release(&ev->xkey);
  else if (ev->type == KeyPress)
    x11_handle_key_press(&ev->xkey);
  return 0;
}

int sz_embedder_present(const char *title, int point_w, int point_h,
                        int pixel_w, int pixel_h, const uint8_t *rgba,
                        size_t nbytes) {
  size_t need;
  int width = pixel_w;
  int height = pixel_h;
  Visual *vis;
  uint32_t *dst;
  size_t i;
  size_t n;
  unsigned long rm;
  unsigned long gm;
  unsigned long bm;

  (void)point_w;
  (void)point_h;

  if (g_user_quit)
    return 0;
  if (title && (!g_win_title || strcmp(g_win_title, title) != 0)) {
    free(g_win_title);
    g_win_title = clip_dup(title);
  }
  if (!rgba)
    return 0;
  if (!frame_bytes(width, height, &need))
    return 0;
  if (nbytes < need)
    return 0;
  /* Do not probe DISPLAY after the connection is open. */
  if (!(g_dpy || ensure_window(title, width, height)))
    return 0;
  /* Read events that arrive after the last pump. A pending user resize
   * takes priority over this frame. */
  x11_drain_pending();
  if (!g_dpy || !g_ready)
    return 0; /* shutdown ran during the drain */
  if (!ensure_frame(width, height))
    return 0;
  /* Resize the window for an injected frame size. A queued window manager
   * resize takes priority. The next frame uses that size. */
  if ((g_win_w != width || g_win_h != height) && !resize_queued())
    XResizeWindow(g_dpy, g_win, (unsigned)width, (unsigned)height);

  vis = DefaultVisual(g_dpy, DefaultScreen(g_dpy));
  dst = (uint32_t *)g_img_data;
  n = (size_t)width * (size_t)height;
  rm = vis->red_mask;
  gm = vis->green_mask;
  bm = vis->blue_mask;
  /* Fast path: 0xFF0000 / 0x00FF00 / 0x0000FF (typical TrueColor). */
  if (rm == 0xFF0000UL && gm == 0xFF00UL && bm == 0xFFUL) {
    for (i = 0; i < n; i++) {
      const uint8_t *p = rgba + i * 4;
      dst[i] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }
  } else {
    for (i = 0; i < n; i++) {
      const uint8_t *p = rgba + i * 4;
      dst[i] = place_chan(p[0], rm) | place_chan(p[1], gm) | place_chan(p[2], bm);
    }
  }
  XPutImage(g_dpy, g_win, g_gc, g_img, 0, 0, 0, 0, (unsigned)width,
            (unsigned)height);
  XFlush(g_dpy);
  x11_drain_pending();
  return 1;
}

void sz_embedder_shutdown(void) {
  a11y_shutdown();
  motion_shutdown();
  x11_xim_shutdown();
  if (g_img) {
    /* XDestroyImage frees g_img_data */
    g_img->data = g_img_data;
    XDestroyImage(g_img);
    g_img = NULL;
    g_img_data = NULL;
  } else if (g_img_data) {
    free(g_img_data);
    g_img_data = NULL;
  }
  if (g_dpy) {
    if (g_win)
      XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
  }
  g_dpy = NULL;
  g_win = 0;
  g_ready = 0;
  g_probed = 0;
  g_probe_ok = 0;
  g_w = g_h = 0;
  g_win_w = g_win_h = 0;
  g_q_head = g_q_tail = 0;
  q_clear_a11y_text();
  g_key_repeat_pending = 0;
  g_key_held = 0;
  g_clip_own = 0;
  g_atom_clipboard = g_atom_utf8 = g_atom_targets = g_atom_prop = 0;
  free(g_win_title);
  g_win_title = NULL;
  free(g_clip);
  g_clip = NULL;
}
