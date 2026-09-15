/* Run the UIKit viewport and keyboard proof on a simulator. */
#import <UIKit/UIKit.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scuzz_mobile.h"

UIViewController *scuzz_ios_make_controller(void);
CGRect scuzz_ios_viewport(void);

static void proof_check(BOOL ok, NSString *message) {
  if (!ok) {
    fprintf(stderr, "ios viewport proof fails: %s\n", message.UTF8String);
    fflush(stderr);
    exit(1);
  }
}

@interface ViewportProof : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UIViewController *controller;
@end

@implementation ViewportProof {
  CGRect _initial;
  int _attempts;
}

- (UIView *)content { return self.controller.view.subviews.firstObject; }

- (void)checkLayout {
  UIView *container = self.controller.view;
  [container layoutIfNeeded];
  CGRect safe = container.safeAreaLayoutGuide.layoutFrame;
  CGRect content = self.content.frame;
  proof_check(fabs(content.origin.x - safe.origin.x) < 1, @"safe leading");
  proof_check(fabs(content.origin.y - safe.origin.y) < 1, @"safe top");
  proof_check(fabs(content.size.width - safe.size.width) < 1, @"safe width");
  proof_check(CGRectGetMaxY(content) <= CGRectGetMaxY(safe) + 1, @"safe bottom");
  proof_check(content.size.height > 0, @"positive viewport");
  SzInputEvent event;
  int resize = 0;
  while (sz_mobile_poll_event(&event)) {
    if (event.kind == SZ_INPUT_RESIZE) {
      resize++;
      proof_check(event.width == (int)content.size.width, @"resize width");
      proof_check(event.height == (int)content.size.height, @"resize height");
      proof_check(event.scale == self.window.screen.scale, @"display scale");
    }
  }
  proof_check(resize == 1, @"one current resize");
}

- (void)startProof {
  [self checkLayout];
  _initial = scuzz_ios_viewport();
  proof_check(_initial.size.height < self.window.bounds.size.height, @"safe area");
  [self.content setNeedsLayout];
  [self.content layoutIfNeeded];
  SzInputEvent event;
  proof_check(!sz_mobile_poll_event(&event), @"unchanged layout has no event");
  sz_mobile_set_keyboard(1);
  [self performSelector:@selector(checkKeyboard) withObject:nil afterDelay:0.1];
}

- (void)checkKeyboard {
  UITextField *field = (UITextField *)self.content.subviews.firstObject;
  proof_check(field.isFirstResponder, @"keyboard focus");
  [self.controller.view layoutIfNeeded];
  CGRect viewport = scuzz_ios_viewport();
  if (viewport.size.height >= _initial.size.height && ++_attempts < 100) {
    [self performSelector:_cmd withObject:nil afterDelay:0.1];
    return;
  }
  proof_check(viewport.size.height < _initial.size.height, @"keyboard shrinks viewport");
  [self checkLayout];
  [field insertText:@"caf\u00e9"];
  SzInputEvent event;
  proof_check(sz_mobile_poll_event(&event), @"keyboard input event");
  proof_check(event.kind == SZ_INPUT_TEXT_EDIT &&
            strcmp(event.text, "caf\xc3\xa9") == 0, @"UTF-8 input");
  [field deleteBackward];
  proof_check(sz_mobile_poll_event(&event), @"backspace event");
  proof_check(event.kind == SZ_INPUT_TEXT_EDIT && !event.text[0], @"backspace");
  sz_mobile_set_keyboard(0);
  _attempts = 0;
  [self performSelector:@selector(checkDismissal) withObject:nil afterDelay:0.1];
}

- (void)checkDismissal {
  [self.controller.view layoutIfNeeded];
  CGRect viewport = scuzz_ios_viewport();
  if (viewport.size.height != _initial.size.height && ++_attempts < 100) {
    [self performSelector:_cmd withObject:nil afterDelay:0.1];
    return;
  }
  proof_check(viewport.size.height == _initial.size.height, @"keyboard restores viewport");
  [self checkLayout];
  UITextField *field = (UITextField *)self.content.subviews.firstObject;
  proof_check(!field.isFirstResponder, @"keyboard dismisses");
  /* Change the container size through UIKit. The same callback handles rotation. */
  self.controller.view.frame = CGRectMake(0, 0, _initial.size.height,
                                          _initial.size.width);
  [self checkLayout];
  viewport = scuzz_ios_viewport();
  proof_check(viewport.size.width != _initial.size.width, @"viewport follows container");
  sz_mobile_shutdown();
  puts("ios viewport proof ok");
  fflush(stdout);
  exit(0);
}

- (BOOL)application:(UIApplication *)app
    didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)app;
  (void)options;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.controller = scuzz_ios_make_controller();
  self.window.rootViewController = self.controller;
  [self.window makeKeyAndVisible];
  [self performSelector:@selector(startProof) withObject:nil afterDelay:0.5];
  return YES;
}
@end

int main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass(ViewportProof.class));
  }
}
