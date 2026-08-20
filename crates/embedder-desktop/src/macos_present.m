#include "scuzz_embedder.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 64

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
    if (g_queue[i].kind == SZ_INPUT_TEXT_EDIT && g_queue[i].text == slot)
      return 1;
  }
  return 0;
}

static const char *stash_text(const char *s) {
  size_t n;
  char *dst;
  if (!s)
    s = "";
  n = strlen(s);
  if (n >= TEXT_LEN)
    n = TEXT_LEN - 1;
  dst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  memcpy(dst, s, n);
  dst[n] = '\0';
  return dst;
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

static void enqueue_pointer(SzPointerPhase phase, float x, float y) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
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

static void enqueue_text_edit(const char *text) {
  SzInputEvent ev;
  /* Drop if the queue is full, or if the next text slot is still queued. */
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i]))
    return;
  memset(&ev, 0, sizeof(ev));
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text ? text : "");
  q_push(&ev);
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
            enqueue_pointer(SZ_POINTER_DOWN, x, y);
            continue;
          }
          if (t == NSEventTypeLeftMouseDragged && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_MOVE, x, y);
            continue;
          }
          if (t == NSEventTypeLeftMouseUp && event_content_xy(ev, &x, &y)) {
            enqueue_pointer(SZ_POINTER_UP, x, y);
            continue;
          }
          /* scrollingDeltaY: positive = content up (matches SZ_INPUT_SCROLL). */
          if (t == NSEventTypeScrollWheel && event_content_xy(ev, &x, &y)) {
            enqueue_scroll(x, y, (float)[ev scrollingDeltaY]);
            continue;
          }
        }

        if ([ev type] == NSEventTypeKeyDown) {
          NSString *chars = [ev characters];
          unichar c =
              (chars && [chars length] > 0) ? [chars characterAtIndex:0] : 0;
          if (c == 'q' || c == 'Q' || c == 27) {
            quit = 1;
            break;
          }
          if (c == 127 || c == NSDeleteCharacter) {
            enqueue_text_edit("");
            continue;
          }
          if (c >= 32 && c < 127) {
            char buf[2] = {(char)c, '\0'};
            enqueue_text_edit(buf);
            continue;
          }
          continue; /* swallow other keydowns (no system beep path) */
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

void sz_embedder_shutdown(void) {
  on_main(^{
    @autoreleasepool {
      shutdown_on_main();
    }
  });
}
