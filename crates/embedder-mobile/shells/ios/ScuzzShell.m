/* Mobile embedder for iOS. Strong sz_mobile_* defs override the weak
 * runtime stubs (crates/runtime/src/ui.c). Present copies the RGBA8888
 * frame to the view on the main queue. Touch and keyboard arrive on the
 * main thread and are polled once per pump on the worker thread. */

#import <CoreGraphics/CoreGraphics.h>
#import <UIKit/UIKit.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "scuzz_mobile.h"

#define EVENT_CAP 64

/* --- Event queue (main thread pushes, worker thread polls) --------------- */

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static os_unfair_lock g_q_lock = OS_UNFAIR_LOCK_INIT;
static _Atomic int g_alive;

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return atomic_load(&g_alive); }

void scuzz_ios_set_alive(int alive) { atomic_store(&g_alive, alive); }

void sz_mobile_shutdown(void) { scuzz_ios_set_alive(0); }

int sz_mobile_push_event(const SzInputEvent *event) {
  int next;
  if (!event)
    return 0;
  os_unfair_lock_lock(&g_q_lock);
  next = (g_q_tail + 1) % EVENT_CAP;
  if (next == g_q_head) {
    os_unfair_lock_unlock(&g_q_lock);
    return 0; /* full */
  }
  g_queue[g_q_tail] = *event;
  g_q_tail = next;
  os_unfair_lock_unlock(&g_q_lock);
  return 1;
}

int sz_mobile_poll_event(SzInputEvent *out) {
  if (!out)
    return 0;
  os_unfair_lock_lock(&g_q_lock);
  if (g_q_head == g_q_tail) {
    os_unfair_lock_unlock(&g_q_lock);
    return 0;
  }
  *out = g_queue[g_q_head];
  g_q_head = (g_q_head + 1) % EVENT_CAP;
  os_unfair_lock_unlock(&g_q_lock);
  return 1;
}

/* --- View ---------------------------------------------------------------- */

@interface ScuzzView : UIView {
  uint8_t *_pixels;
  size_t _nbytes;
  int _pw;
  int _ph;
  os_unfair_lock _lock;
}
- (void)setFramePixels:(NSData *)data width:(int)w height:(int)h;
@end

@implementation ScuzzView

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    _lock = OS_UNFAIR_LOCK_INIT;
    self.backgroundColor = [UIColor whiteColor];
  }
  return self;
}

- (void)dealloc {
  free(_pixels);
}

- (void)setFramePixels:(NSData *)data width:(int)w height:(int)h {
  os_unfair_lock_lock(&_lock);
  free(_pixels);
  _nbytes = [data length];
  _pixels = (uint8_t *)malloc(_nbytes);
  memcpy(_pixels, [data bytes], _nbytes);
  _pw = w;
  _ph = h;
  os_unfair_lock_unlock(&_lock);
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect {
  os_unfair_lock_lock(&_lock);
  if (_pixels && _pw > 0 && _ph > 0) {
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider =
        CGDataProviderCreateWithData(NULL, _pixels, _nbytes, NULL);
    CGImageRef image = CGImageCreate(
        (size_t)_pw, (size_t)_ph, 8, 32, (size_t)_pw * 4, cs,
        kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast, provider,
        NULL, false, kCGRenderingIntentDefault);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSaveGState(ctx);
    /* UIKit draws with the y axis flipped against the RGBA raster. */
    CGContextTranslateCTM(ctx, 0, self.bounds.size.height);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextDrawImage(ctx, self.bounds, image);
    CGContextRestoreGState(ctx);
    CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(cs);
  }
  os_unfair_lock_unlock(&_lock);
}

- (void)pushPointer:(NSSet<UITouch *> *)touches phase:(SzPointerPhase)phase {
  UITouch *touch = [touches anyObject];
  CGPoint p = [touch locationInView:self];
  SzInputEvent ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_POINTER;
  ev.pointer_phase = phase;
  ev.x = (float)p.x;
  ev.y = (float)p.y;
  sz_mobile_push_event(&ev);
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  [self pushPointer:touches phase:SZ_POINTER_DOWN];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  [self pushPointer:touches phase:SZ_POINTER_MOVE];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  [self pushPointer:touches phase:SZ_POINTER_UP];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches
               withEvent:(UIEvent *)event {
  [self pushPointer:touches phase:SZ_POINTER_UP];
}

@end

/* --- Shell hooks --------------------------------------------------------- */

static ScuzzView *g_view;
static UITextField *g_keyboard_field;

UIView *scuzz_ios_make_view(CGRect bounds) {
  g_view = [[ScuzzView alloc] initWithFrame:bounds];
  /* Hidden field that summons the soft keyboard for TextField focus. */
  g_keyboard_field = [[UITextField alloc] initWithFrame:CGRectZero];
  g_keyboard_field.hidden = YES;
  [g_view addSubview:g_keyboard_field];
  return g_view;
}

int sz_mobile_present(const char *title, int width, int height,
                      const uint8_t *rgba, size_t nbytes) {
  NSData *frame;
  (void)title;
  if (!rgba || width <= 0 || height <= 0)
    return 0;
  if (nbytes < (size_t)width * (size_t)height * 4)
    return 0;
  /* Copy: the worker thread reuses the raster on the next pump. */
  frame = [NSData dataWithBytes:rgba length:nbytes];
  dispatch_async(dispatch_get_main_queue(), ^{
    [g_view setFramePixels:frame width:width height:height];
  });
  return 1;
}

void sz_mobile_set_keyboard(int visible) {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_keyboard_field)
      return;
    if (visible)
      [g_keyboard_field becomeFirstResponder];
    else
      [g_keyboard_field resignFirstResponder];
  });
}
