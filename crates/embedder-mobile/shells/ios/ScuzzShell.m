/* iOS sz_mobile_* shell. Strong defs override the weak runtime stubs. */

#import <CoreGraphics/CoreGraphics.h>
#import <UIKit/UIKit.h>

#include <os/lock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "scuzz_mobile.h"

#define EVENT_CAP 64
#define TEXT_RING 64
#define TEXT_LEN 64

/* --- Event queue (main thread pushes, worker thread polls) --------------- */

static SzInputEvent g_queue[EVENT_CAP];
static int g_q_head;
static int g_q_tail;
static os_unfair_lock g_q_lock = OS_UNFAIR_LOCK_INIT;
static _Atomic int g_alive;
static char g_text_bufs[TEXT_RING][TEXT_LEN];
static int g_text_i;
static char g_poll_text[TEXT_LEN];
static int g_soft_keyboard;
static UIView *g_hidden_input;

static int q_full(void) { return ((g_q_tail + 1) % EVENT_CAP) == g_q_head; }

static int text_slot_queued(const char *slot) {
  int i;
  for (i = g_q_head; i != g_q_tail; i = (i + 1) % EVENT_CAP) {
    if (g_queue[i].kind == SZ_INPUT_TEXT_EDIT && g_queue[i].text == slot)
      return 1;
  }
  return 0;
}

static void copy_text(char *dst, const char *s) {
  size_t n;
  if (!s)
    s = "";
  n = strlen(s);
  if (n >= TEXT_LEN)
    n = TEXT_LEN - 1;
  memcpy(dst, s, n);
  dst[n] = '\0';
}

static const char *stash_text(const char *s) {
  char *dst = g_text_bufs[g_text_i];
  g_text_i = (g_text_i + 1) % TEXT_RING;
  copy_text(dst, s);
  return dst;
}

static int frame_bytes(int width, int height, size_t *out) {
  size_t w;
  size_t h;
  if (width <= 0 || height <= 0 || !out)
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

static void enqueue_text_edit(const char *text) {
  SzInputEvent ev;
  os_unfair_lock_lock(&g_q_lock);
  if (q_full() || text_slot_queued(g_text_bufs[g_text_i])) {
    os_unfair_lock_unlock(&g_q_lock);
    return;
  }
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_TEXT_EDIT;
  ev.text = stash_text(text);
  g_queue[g_q_tail] = ev;
  g_q_tail = (g_q_tail + 1) % EVENT_CAP;
  os_unfair_lock_unlock(&g_q_lock);
}

int sz_mobile_available(void) { return 1; }

int sz_mobile_alive(void) { return atomic_load(&g_alive); }

void scuzz_ios_set_alive(int alive) { atomic_store(&g_alive, alive ? 1 : 0); }

void scuzz_ios_push_lifecycle(int phase) {
  SzInputEvent ev;
  if (phase != SZ_LIFECYCLE_RESUME && phase != SZ_LIFECYCLE_PAUSE &&
      phase != SZ_LIFECYCLE_STOP)
    return;
  memset(&ev, 0, sizeof ev);
  ev.kind = SZ_INPUT_LIFECYCLE;
  ev.lifecycle = (SzLifecyclePhase)phase;
  sz_mobile_push_event(&ev);
}

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
  if (out->kind == SZ_INPUT_TEXT_EDIT) {
    copy_text(g_poll_text, out->text);
    out->text = g_poll_text;
  }
  os_unfair_lock_unlock(&g_q_lock);
  return 1;
}

/* --- View ---------------------------------------------------------------- */

@interface ScuzzView : UIView {
  uint8_t *_pixels;
  size_t _nbytes;
  int _pw;
  int _ph;
}
- (void)setFramePixels:(NSData *)data width:(int)w height:(int)h;
@end

@implementation ScuzzView

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self)
    self.backgroundColor = [UIColor whiteColor];
  return self;
}

- (void)dealloc {
  free(_pixels);
}

- (void)setFramePixels:(NSData *)data width:(int)w height:(int)h {
  size_t n = data ? [data length] : 0;
  uint8_t *next = NULL;
  if (n > 0) {
    next = (uint8_t *)realloc(_pixels, n);
    if (!next)
      return;
    memcpy(next, [data bytes], n);
  } else {
    free(_pixels);
  }
  _pixels = next;
  _nbytes = n;
  _pw = w;
  _ph = h;
  [self setNeedsDisplay];
}

- (void)drawRect:(CGRect)rect {
  (void)rect;
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
  (void)event;
  [self pushPointer:touches phase:SZ_POINTER_DOWN];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  [self pushPointer:touches phase:SZ_POINTER_MOVE];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  [self pushPointer:touches phase:SZ_POINTER_UP];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches
               withEvent:(UIEvent *)event {
  (void)event;
  [self pushPointer:touches phase:SZ_POINTER_UP];
}

@end

/* Invisible first responder. Insert and backspace become TEXT_EDIT.
 * Soft keyboard stays off until sz_mobile_set_keyboard(1). Hardware keys
 * still reach the field so the simulator can type without a tap. */
@interface ScuzzKeyboardField : UITextField <UITextFieldDelegate>
@end

@implementation ScuzzKeyboardField
- (UIView *)inputView {
  if (g_soft_keyboard)
    return nil;
  if (!g_hidden_input)
    g_hidden_input = [[UIView alloc] initWithFrame:CGRectZero];
  return g_hidden_input;
}

- (BOOL)textField:(UITextField *)textField
    shouldChangeCharactersInRange:(NSRange)range
                replacementString:(NSString *)string {
  (void)textField;
  if ([string length] == 0) {
    NSUInteger n = range.length > 0 ? range.length : 1;
    NSUInteger i;
    for (i = 0; i < n; i++)
      enqueue_text_edit("");
  } else {
    const char *utf8 = [string UTF8String];
    enqueue_text_edit(utf8 ? utf8 : "");
  }
  return NO;
}

- (void)insertText:(NSString *)text {
  const char *utf8 = [text UTF8String];
  if (utf8 && utf8[0])
    enqueue_text_edit(utf8);
}

- (void)deleteBackward {
  enqueue_text_edit("");
}
@end

/* --- Shell hooks --------------------------------------------------------- */

static ScuzzView *g_view;
static ScuzzKeyboardField *g_keyboard_field;

UIView *scuzz_ios_make_view(CGRect bounds) {
  g_view = [[ScuzzView alloc] initWithFrame:bounds];
  /* Alpha 0: a hidden view cannot become first responder. */
  g_keyboard_field = [[ScuzzKeyboardField alloc]
      initWithFrame:CGRectMake(0, 0, 1, 1)];
  g_keyboard_field.alpha = 0;
  g_keyboard_field.userInteractionEnabled = NO;
  g_keyboard_field.autocorrectionType = UITextAutocorrectionTypeNo;
  g_keyboard_field.autocapitalizationType = UITextAutocapitalizationTypeNone;
  g_keyboard_field.spellCheckingType = UITextSpellCheckingTypeNo;
  g_keyboard_field.delegate = g_keyboard_field;
  g_keyboard_field.text = @"\u200b";
  [g_view addSubview:g_keyboard_field];
  dispatch_async(dispatch_get_main_queue(), ^{
    const char *typed;
    [g_keyboard_field becomeFirstResponder];
    typed = getenv("SCUZZ_IOS_TYPE");
    if (!typed || !typed[0])
      return;
    /* Same UITextField delegate path as a keystroke. Delay until Ui.run
     * has mounted and is polling. */
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(800 * NSEC_PER_MSEC)),
        dispatch_get_main_queue(), ^{
          NSString *s = [NSString stringWithUTF8String:typed];
          id<UITextFieldDelegate> del = g_keyboard_field.delegate;
          NSRange insert = NSMakeRange(1, 0);
          NSRange chop = NSMakeRange(0, 1);
          (void)[del textField:g_keyboard_field
              shouldChangeCharactersInRange:insert
                          replacementString:s];
          (void)[del textField:g_keyboard_field
              shouldChangeCharactersInRange:chop
                          replacementString:@""];
        });
  });
  return g_view;
}

int sz_mobile_present(const char *title, int point_w, int point_h, int pixel_w,
                      int pixel_h, const uint8_t *rgba, size_t nbytes) {
  NSData *frame;
  size_t need;
  (void)title;
  (void)point_w;
  (void)point_h;
  if (!rgba)
    return 0;
  if (!frame_bytes(pixel_w, pixel_h, &need))
    return 0;
  if (nbytes < need)
    return 0;
  /* Copy: the worker thread reuses the raster on the next pump. */
  frame = [NSData dataWithBytes:rgba length:need];
  dispatch_async(dispatch_get_main_queue(), ^{
    [g_view setFramePixels:frame width:pixel_w height:pixel_h];
  });
  return 1;
}

void sz_mobile_set_keyboard(int visible) {
  dispatch_async(dispatch_get_main_queue(), ^{
    if (!g_keyboard_field)
      return;
    g_soft_keyboard = visible ? 1 : 0;
    [g_keyboard_field reloadInputViews];
    if (![g_keyboard_field isFirstResponder])
      [g_keyboard_field becomeFirstResponder];
  });
}
