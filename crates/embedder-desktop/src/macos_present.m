#include "scuzz_embedder.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 128
#define KEY_NAME_LEN 32

static NSWindow *g_win;
static NSImageView *g_view;
static int g_w;
static int g_h;
static int g_ready;
static int g_app_ready;
static int g_user_quit;

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static char g_key_bufs[TEXT_RING][KEY_NAME_LEN];
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;

int sz_embedder_available(void) {
  /* GUI session with a main display (SSH/headless Mac returns 0). */
  return CGMainDisplayID() != kCGNullDirectDisplay ? 1 : 0;
}

int sz_embedder_alive(void) {
  return !g_user_quit && sz_embedder_available();
}

/* AppKit must run on the process main thread. The Scuzz IO runtime often
 * executes on a large-stack worker. Hop through the main queue (main parks in
 * CFRunLoop — see sz_runtime_main_args). */
static void on_main(void (^block)(void)) {
  if ([NSThread isMainThread]) {
    block();
  } else {
    dispatch_sync(dispatch_get_main_queue(), block);
  }
}

double sz_embedder_display_scale(void) {
  __block double scale = 1.0;
  if (!sz_embedder_available())
    return 1.0;
  on_main(^{
    NSScreen *screen = [NSScreen mainScreen];
    if (screen)
      scale = (double)[screen backingScaleFactor];
    if (scale < 1.0)
      scale = 1.0;
  });
  return scale;
}

static int q_full(void) {
  return ((g_q_tail + 1) % EVENT_CAP) == g_q_head;
}

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    if (g_queue[i].kind == SZ_INPUT_KEY &&
        (g_queue[i].key == slot || g_queue[i].text == slot))
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
    return 0;
  g_queue[g_q_tail] = *ev;
  g_q_tail = next;
  return 1;
}

int sz_embedder_poll_event(SzInputEvent *out) {
  if (!out || g_q_head == g_q_tail)
    return 0;
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  return 1;
}

static void enqueue_pointer(SzPointerPhase phase, float x, float y, int button) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
  ev.pointer_button = button;
  ev.x = x;
  ev.y = y;
  q_push(&ev);
}

static void enqueue_scroll(float x, float y, float dy) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_SCROLL;
  ev.x = x;
  ev.y = y;
  ev.dy = dy;
  q_push(&ev);
}

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

static int cocoa_key_no_insert(const char *name) {
  return strcmp(name, "Backspace") == 0 || strcmp(name, "Enter") == 0 ||
         strcmp(name, "Tab") == 0 || strcmp(name, "Escape") == 0 ||
         strcmp(name, "Delete") == 0 || strncmp(name, "Arrow", 5) == 0 ||
         strcmp(name, "Home") == 0 || strcmp(name, "End") == 0 ||
         strcmp(name, "PageUp") == 0 || strcmp(name, "PageDown") == 0;
}

static void cocoa_key_name(unichar c, char *out, size_t cap) {
  const char *s = NULL;
  if (!out || cap == 0)
    return;
  out[0] = '\0';
  switch (c) {
  case NSEnterCharacter:
  case NSCarriageReturnCharacter:
  case NSNewlineCharacter:
    s = "Enter";
    break;
  case NSTabCharacter:
    s = "Tab";
    break;
  case 0x08:
  case NSDeleteCharacter:
    s = "Backspace";
    break;
  case NSDeleteFunctionKey:
    s = "Delete";
    break;
  case 0x1b:
    s = "Escape";
    break;
  case NSLeftArrowFunctionKey:
    s = "ArrowLeft";
    break;
  case NSRightArrowFunctionKey:
    s = "ArrowRight";
    break;
  case NSUpArrowFunctionKey:
    s = "ArrowUp";
    break;
  case NSDownArrowFunctionKey:
    s = "ArrowDown";
    break;
  case NSHomeFunctionKey:
    s = "Home";
    break;
  case NSEndFunctionKey:
    s = "End";
    break;
  case NSPageUpFunctionKey:
    s = "PageUp";
    break;
  case NSPageDownFunctionKey:
    s = "PageDown";
    break;
  case ' ':
    s = "Space";
    break;
  default:
    break;
  }
  if (s) {
    snprintf(out, cap, "%s", s);
    return;
  }
  if (c >= 'A' && c <= 'Z') {
    out[0] = (char)('a' + (c - 'A'));
    out[1] = '\0';
    return;
  }
  if (c >= 32 && c < 127) {
    out[0] = (char)c;
    out[1] = '\0';
    return;
  }
  {
    unichar u = c;
    NSString *one = [NSString stringWithCharacters:&u length:1];
    const char *utf8 = one ? [one UTF8String] : NULL;
    if (utf8 && utf8[0]) {
      snprintf(out, cap, "%s", utf8);
      return;
    }
  }
  snprintf(out, cap, "Unidentified");
}

/* Convert a window event to Scuzz layout coords. 0 if outside the content view. */
static int event_content_xy(NSEvent *ev, float *x, float *y) {
  NSView *content;
  NSPoint loc;
  NSPoint inView;
  if (!ev || !g_win || !x || !y)
    return 0;
  content = [g_win contentView];
  if (!content)
    return 0;
  loc = [ev locationInWindow];
  inView = [content convertPoint:loc fromView:nil];
  if (!NSPointInRect(inView, [content bounds]))
    return 0;
  *x = (float)inView.x;
  *y = (float)(content.bounds.size.height - inView.y);
  return 1;
}

static void ensure_app(void) {
  if (g_app_ready)
    return;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  g_app_ready = 1;
}

static void shutdown_on_main(void) {
  if (g_win) {
    [g_win orderOut:nil];
    [g_win close];
    g_win = nil;
  }
  g_view = nil;
  g_ready = 0;
  g_w = g_h = 0;
  g_q_head = g_q_tail = 0;
}

static void mark_user_quit(void) {
  g_user_quit = 1;
  sz_embedder_shutdown();
}

static int ensure_window_on_main(const char *title, int width, int height) {
  if (g_ready && g_w == width && g_h == height)
    return 1;

  shutdown_on_main();
  g_user_quit = 0;
  ensure_app();

  NSRect rect = NSMakeRect(100, 100, (CGFloat)width, (CGFloat)height);
  NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                     NSWindowStyleMaskMiniaturizable;
  g_win = [[NSWindow alloc] initWithContentRect:rect
                                      styleMask:style
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  if (!g_win) {
    fprintf(stderr, "scuzz embedder: cannot create NSWindow\n");
    return 0;
  }

  NSString *nsTitle =
      title ? [NSString stringWithUTF8String:title] : @"Scuzz Lang";
  [g_win setTitle:nsTitle];
  [g_win setReleasedWhenClosed:NO];

  g_view = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
  [g_view setImageScaling:NSImageScaleAxesIndependently];
  [g_view setAnimates:NO];
  [g_win setContentView:g_view];
  [g_win setAcceptsMouseMovedEvents:YES];
  [g_win makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  g_w = width;
  g_h = height;
  g_ready = 1;
  fprintf(stderr, "scuzz embedder: Cocoa window %dx%d\n", width, height);
  return 1;
}

int sz_embedder_present(const char *title, int point_w, int point_h,
                        int pixel_w, int pixel_h, const uint8_t *rgba,
                        size_t nbytes) {
  size_t need;
  __block int ok = 0;
  __block int quit = 0;

  if (g_user_quit)
    return 0;
  if (!rgba || point_w <= 0 || point_h <= 0 || pixel_w <= 0 || pixel_h <= 0)
    return 0;
  need = (size_t)pixel_w * (size_t)pixel_h * 4;
  if (nbytes < need)
    return 0;
  if (!sz_embedder_available())
    return 0;

  on_main(^{
    @autoreleasepool {
      if (!ensure_window_on_main(title, point_w, point_h))
        return;

      NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
          initWithBitmapDataPlanes:NULL
                        pixelsWide:pixel_w
                        pixelsHigh:pixel_h
                     bitsPerSample:8
                   samplesPerPixel:4
                          hasAlpha:YES
                          isPlanar:NO
                    colorSpaceName:NSDeviceRGBColorSpace
                       bytesPerRow:(NSInteger)pixel_w * 4
                      bitsPerPixel:32];
      if (!rep || ![rep bitmapData]) {
        fprintf(stderr, "scuzz embedder: bitmap alloc failed\n");
        return;
      }
      memcpy([rep bitmapData], rgba, need);
      /* Point size + pixel buffer → sharp Retina blit (no stretch upsample). */
      [rep setSize:NSMakeSize((CGFloat)point_w, (CGFloat)point_h)];

      NSImage *image = [[NSImage alloc]
          initWithSize:NSMakeSize((CGFloat)point_w, (CGFloat)point_h)];
      [image addRepresentation:rep];
      [g_view setImageScaling:NSImageScaleAxesIndependently];
      [g_view setImage:image];
      [g_view setNeedsDisplay:YES];
      [g_win displayIfNeeded];

      /* Drain pending events: quit handled here; input only enqueued. */
      for (;;) {
        NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate distantPast]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (!ev)
          break;

        {
          NSEventType t = [ev type];
          float x, y;
          if (t == NSEventTypeLeftMouseDown && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_DOWN, x, y, 1);
            continue;
          }
          if (t == NSEventTypeLeftMouseDragged && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_MOVE, x, y, 1);
            continue;
          }
          if (t == NSEventTypeLeftMouseUp && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_UP, x, y, 1);
            continue;
          }
          if (t == NSEventTypeRightMouseDown && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_DOWN, x, y, 3);
            continue;
          }
          if (t == NSEventTypeRightMouseDragged && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_MOVE, x, y, 3);
            continue;
          }
          if (t == NSEventTypeRightMouseUp && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_UP, x, y, 3);
            continue;
          }
          if (t == NSEventTypeMouseMoved && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_MOVE, x, y, 0);
            continue;
          }
          /* scrollingDeltaY: positive = content up (matches SZ_INPUT_SCROLL). */
          if (t == NSEventTypeScrollWheel && event_content_xy(ev, &x, &y)) {
            enqueue_scroll(x, y, (float)[ev scrollingDeltaY]);
            continue;
          }
        }

        if ([ev type] == NSEventTypeKeyDown) {
          NSString *ign = [ev charactersIgnoringModifiers];
          NSString *chars = [ev characters];
          unichar c =
              (ign && [ign length] > 0) ? [ign characterAtIndex:0] : 0;
          char name[KEY_NAME_LEN];
          char utf8[TEXT_LEN];
          int mods = 0;
          NSEventModifierFlags flags = [ev modifierFlags];
          cocoa_key_name(c, name, sizeof name);
          utf8[0] = '\0';
          if (!cocoa_key_no_insert(name) && chars && [chars length] > 0) {
            const char *u = [chars UTF8String];
            if (u && (unsigned char)u[0] >= 32)
              snprintf(utf8, sizeof utf8, "%s", u);
          }
          if (flags & NSEventModifierFlagShift)
            mods |= SZ_KEY_SHIFT;
          if (flags & NSEventModifierFlagControl)
            mods |= SZ_KEY_CTRL;
          if (flags & NSEventModifierFlagCommand)
            mods |= SZ_KEY_CMD;
          if (flags & NSEventModifierFlagOption)
            mods |= SZ_KEY_ALT;
          enqueue_key(name, utf8, mods, [ev isARepeat] ? 1 : 0);
          continue; /* swallow keydowns (no system beep path) */
        }

        [NSApp sendEvent:ev];
      }

      if (!quit && g_win && ![g_win isVisible])
        quit = 1;
      ok = 1;
    }
  });

  if (!ok)
    return 0;
  if (quit) {
    mark_user_quit();
    return 1;
  }
  return 1;
}

int sz_embedder_clipboard_set(const char *text) {
  __block int ok = 0;
  NSString *s = [NSString stringWithUTF8String:text ? text : ""];
  if (!s)
    s = @"";
  on_main(^{
    @autoreleasepool {
      NSPasteboard *pb = [NSPasteboard generalPasteboard];
      [pb clearContents];
      ok = [pb setString:s forType:NSPasteboardTypeString] ? 1 : 0;
    }
  });
  return ok;
}

char *sz_embedder_clipboard_get(void) {
  __block char *out = NULL;
  on_main(^{
    @autoreleasepool {
      NSPasteboard *pb = [NSPasteboard generalPasteboard];
      NSString *s = [pb stringForType:NSPasteboardTypeString];
      const char *u = s ? [s UTF8String] : NULL;
      if (u) {
        size_t n = strlen(u);
        out = (char *)malloc(n + 1);
        if (out)
          memcpy(out, u, n + 1);
      }
    }
  });
  return out;
}

void sz_embedder_shutdown(void) {
  on_main(^{
    @autoreleasepool {
      shutdown_on_main();
    }
  });
}
