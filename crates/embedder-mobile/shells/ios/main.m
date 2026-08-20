/* iOS shell entry. UIApplicationMain owns the main thread. The Scuzz app
 * main (renamed to scuzz_app_main at link time) runs on a worker thread and
 * mounts UiRuntime.Mobile. Frames and input cross through the sz_mobile_*
 * hooks in ScuzzShell.m. */

#import <UIKit/UIKit.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "scuzz_mobile.h"

extern int scuzz_app_main(int argc, char **argv);

void scuzz_ios_set_alive(int alive);
void scuzz_ios_push_lifecycle(int phase);
UIView *scuzz_ios_make_view(CGRect bounds);

static void *scuzz_app_thread(void *unused) {
  char *args[] = {(char *)"scuzz", NULL};
  (void)unused;
  scuzz_app_main(1, args);
  return NULL;
}

@interface ScuzzAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation ScuzzAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
  CGRect bounds = [UIScreen mainScreen].bounds;
  CGFloat nativeScale = [UIScreen mainScreen].nativeScale;
  pthread_t tid;
  (void)application;
  (void)launchOptions;
  self.window = [[UIWindow alloc] initWithFrame:bounds];
  UIViewController *vc = [[UIViewController alloc] init];
  vc.view = scuzz_ios_make_view(bounds);
  self.window.rootViewController = vc;
  [self.window makeKeyAndVisible];

  /* Ui.run reads session config from env on the worker thread. */
  char width[16];
  char height[16];
  char scale[16];
  snprintf(width, sizeof width, "%d", (int)bounds.size.width);
  snprintf(height, sizeof height, "%d", (int)bounds.size.height);
  snprintf(scale, sizeof scale, "%g", (double)nativeScale);
  setenv("SCUZZ_UI_RUNTIME", "mobile", 1);
  setenv("SCUZZ_UI_WIDTH", width, 1);
  setenv("SCUZZ_UI_HEIGHT", height, 1);
  setenv("SCUZZ_UI_SCALE", scale, 1);
  {
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                        NSUserDomainMask, YES);
    NSString *docs = dirs.firstObject;
    NSString *dump =
        [docs stringByAppendingPathComponent:@"scuzz_ios.debug.dump"];
    setenv("SCUZZ_UI_DEBUG_DUMP", dump.fileSystemRepresentation, 0);
  }

  scuzz_ios_set_alive(1);
  if (pthread_create(&tid, NULL, scuzz_app_thread, NULL) != 0) {
    scuzz_ios_set_alive(0);
    return YES;
  }
  pthread_detach(tid);
  return YES;
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
  (void)application;
  scuzz_ios_push_lifecycle(SZ_LIFECYCLE_PAUSE);
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
  (void)application;
  scuzz_ios_push_lifecycle(SZ_LIFECYCLE_RESUME);
}

- (void)applicationWillTerminate:(UIApplication *)application {
  (void)application;
  scuzz_ios_push_lifecycle(SZ_LIFECYCLE_STOP);
  scuzz_ios_set_alive(0);
}

@end

int main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass([ScuzzAppDelegate class]));
  }
}
