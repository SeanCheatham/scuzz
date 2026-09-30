/* The manual AppKit pump must complete application launch. It must also
 * forward input and resize events after the first frame. */
#include "scuzz_embedder.h"

#import <Cocoa/Cocoa.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#define TITLE "scuzz-macos-event-test"
#define W 80
#define H 60
#define W2 120
#define H2 90

static int failures;

static void check(int ok, const char *name) {
  if (ok) {
    printf("ok: %s\n", name);
    return;
  }
  fprintf(stderr, "FAIL: %s\n", name);
  failures++;
}

@interface ScuzzLaunchProbe : NSObject
@property(nonatomic) int finished;
@end

@implementation ScuzzLaunchProbe
- (void)didFinish:(NSNotification *)notification {
  (void)notification;
  self.finished = 1;
}
@end

static void drain_events(void) {
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  while (sz_embedder_poll_event(&ev))
    memset(&ev, 0, sizeof ev);
}

static void post_click(NSWindow *window, NSPoint point) {
  for (int i = 0; i < 2; i++) {
    NSEvent *event = [NSEvent
        mouseEventWithType:i == 0 ? NSEventTypeLeftMouseDown : NSEventTypeLeftMouseUp
                  location:point
             modifierFlags:0
                 timestamp:NSProcessInfo.processInfo.systemUptime
              windowNumber:window.windowNumber
                   context:nil
               eventNumber:i + 1
                clickCount:1
                  pressure:i == 0 ? 1.0 : 0.0];
    [NSApp postEvent:event atStart:NO];
  }
}

int main(void) {
  static unsigned char pixels[W * H * 4];
  ScuzzLaunchProbe *probe;
  NSWindow *window;
  SzInputEvent ev;
  int got_pointer = 0;
  int got_release = 0;
  int got_resize = 0;
  __block int native_down = 0;

  if (!sz_embedder_available()) {
    printf("test_macos_events: skip (no display)\n");
    return 0;
  }

  @autoreleasepool {
    probe = [[ScuzzLaunchProbe alloc] init];
    [[NSNotificationCenter defaultCenter]
        addObserver:probe
           selector:@selector(didFinish:)
               name:NSApplicationDidFinishLaunchingNotification
             object:nil];

    memset(pixels, 0x40, sizeof pixels);
    check(sz_embedder_present(TITLE, W, H, W, H, pixels, sizeof pixels) == 1,
          "present first frame");
    check(probe.finished == 1, "application finishes launch");
    window = [[NSApp windows] firstObject];
    check(window != nil, "window exists");
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                      handler:^NSEvent *(NSEvent *event) {
      native_down++;
      return event;
    }];

    drain_events();
    if (window) {
      [window setContentSize:NSMakeSize(W2, H2)];
      post_click(window, NSMakePoint(30, 30));
    }

    memset(&ev, 0, sizeof ev);
    while (sz_embedder_poll_event(&ev)) {
      if (ev.kind == SZ_INPUT_POINTER &&
          ev.pointer_phase == SZ_POINTER_DOWN) {
        got_pointer = 1;
        if (fabsf(ev.x - 30) >= 1 || fabsf(ev.y - (H2 - 30)) >= 1)
          fprintf(stderr, "pointer: %.1f, %.1f\n", ev.x, ev.y);
        check(fabsf(ev.x - 30) < 1 && fabsf(ev.y - (H2 - 30)) < 1,
              "pointer uses top-left content coordinates");
      }
      if (ev.kind == SZ_INPUT_POINTER && ev.pointer_phase == SZ_POINTER_UP)
        got_release = 1;
      if (ev.kind == SZ_INPUT_RESIZE && ev.width == W2 && ev.height == H2)
        got_resize = 1;
      memset(&ev, 0, sizeof ev);
    }
    check(got_pointer, "poll receives pointer after first frame");
    check(got_release, "poll receives pointer release through AppKit");
    check(got_resize, "poll receives resize after first frame");
    check(native_down == 1, "AppKit receives content input");

    if (window)
      post_click(window, NSMakePoint(1, H2 / 2));
    drain_events();
    check(native_down == 2, "AppKit receives window border input");
    [NSEvent removeMonitor:monitor];

    if (window) {
      NSView *content = window.contentView;
      NSImageView *image = (NSImageView *)content.subviews.firstObject;
      check(NSEqualRects(image.frame, content.bounds),
            "image fills resized content bounds");
      static unsigned char resized[W2 * H2 * 4];
      memset(resized, 0x40, sizeof resized);
      check(sz_embedder_present(TITLE, W2, H2, W2, H2, resized, sizeof resized),
            "present resized frame");
      check(NSEqualRects(image.frame, content.bounds),
            "image fills content after presentation");
    }

    [[NSNotificationCenter defaultCenter] removeObserver:probe];
    sz_embedder_shutdown();
  }

  if (failures) {
    fprintf(stderr, "test_macos_events: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_macos_events: all ok\n");
  return 0;
}
