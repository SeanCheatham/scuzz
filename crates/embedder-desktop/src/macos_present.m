#include "scuzz_embedder.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

#include <stdio.h>
#include <string.h>

static NSWindow *g_win;
static NSImageView *g_view;
static int g_w;
static int g_h;
static int g_ready;
static int g_app_ready;
static int g_user_quit;

int sz_embedder_available(void) {
  /* GUI session with a main display (SSH/headless Mac returns 0). */
  return CGMainDisplayID() != kCGNullDirectDisplay ? 1 : 0;
}

int sz_embedder_alive(void) {
  return !g_user_quit && sz_embedder_available();
}

static void ensure_app(void) {
  if (g_app_ready)
    return;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  g_app_ready = 1;
}

static void mark_user_quit(void) {
  g_user_quit = 1;
  sz_embedder_shutdown();
}

static int ensure_window(const char *title, int width, int height) {
  @autoreleasepool {
    if (g_ready && g_w == width && g_h == height)
      return 1;

    sz_embedder_shutdown();
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
}

int sz_embedder_present(const char *title, int width, int height,
                        const uint8_t *rgba, size_t nbytes) {
  size_t need;
  int quit = 0;

  if (g_user_quit)
    return 0;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  need = (size_t)width * (size_t)height * 4;
  if (nbytes < need)
    return 0;
  if (!sz_embedder_available())
    return 0;
  if (!ensure_window(title, width, height))
    return 0;

  @autoreleasepool {
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:width
                      pixelsHigh:height
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:(NSInteger)width * 4
                    bitsPerPixel:32];
    if (!rep || ![rep bitmapData]) {
      fprintf(stderr, "scuzz embedder: bitmap alloc failed\n");
      return 0;
    }
    memcpy([rep bitmapData], rgba, need);

    NSImage *image =
        [[NSImage alloc] initWithSize:NSMakeSize((CGFloat)width, (CGFloat)height)];
    [image addRepresentation:rep];
    [g_view setImage:image];
    [g_view setNeedsDisplay:YES];
    [g_win displayIfNeeded];

    /* Drain pending events without blocking (peer to XPending loop). */
    for (;;) {
      NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:[NSDate distantPast]
                                          inMode:NSDefaultRunLoopMode
                                         dequeue:YES];
      if (!ev)
        break;
      if ([ev type] == NSEventTypeKeyDown) {
        NSString *chars = [ev charactersIgnoringModifiers];
        unichar c = (chars && [chars length] > 0) ? [chars characterAtIndex:0] : 0;
        if (c == 'q' || c == 'Q' || c == 27) {
          quit = 1;
          break;
        }
      }
      [NSApp sendEvent:ev];
    }

    if (!quit && g_win && ![g_win isVisible])
      quit = 1;
  }

  if (quit) {
    mark_user_quit();
    return 1;
  }
  return 1;
}

void sz_embedder_shutdown(void) {
  @autoreleasepool {
    if (g_win) {
      [g_win orderOut:nil];
      [g_win close];
      g_win = nil;
    }
    g_view = nil;
  }
  g_ready = 0;
  g_w = g_h = 0;
}
