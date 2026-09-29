/* The manual AppKit pump must complete application launch. It must also
 * forward input and resize events after the first frame. */
#include "scuzz_embedder.h"

#import <Cocoa/Cocoa.h>

#include <stdio.h>
#include <string.h>

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

int main(void) {
  static unsigned char pixels[W * H * 4];
  ScuzzLaunchProbe *probe;
  NSWindow *window;
  SzInputEvent ev;
  int got_pointer = 0;
  int got_resize = 0;

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

    drain_events();
    if (window) {
      [window setContentSize:NSMakeSize(W2, H2)];
      NSEvent *down = [NSEvent
          mouseEventWithType:NSEventTypeLeftMouseDown
                    location:NSMakePoint(8, 8)
               modifierFlags:0
                   timestamp:0
                windowNumber:window.windowNumber
                     context:nil
                 eventNumber:1
                  clickCount:1
                    pressure:1.0];
      [NSApp postEvent:down atStart:NO];
    }

    memset(&ev, 0, sizeof ev);
    while (sz_embedder_poll_event(&ev)) {
      if (ev.kind == SZ_INPUT_POINTER &&
          ev.pointer_phase == SZ_POINTER_DOWN)
        got_pointer = 1;
      if (ev.kind == SZ_INPUT_RESIZE && ev.width == W2 && ev.height == H2)
        got_resize = 1;
      memset(&ev, 0, sizeof ev);
    }
    check(got_pointer, "poll receives pointer after first frame");
    check(got_resize, "poll receives resize after first frame");

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
